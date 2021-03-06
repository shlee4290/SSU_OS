#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <pwd.h>
#include <time.h>

#define INIT_LIST_SIZE 1024
#define BUFFER_SIZE 128

unsigned long uptime;

const int NANOS = 1000000000;
const int MILLIS = 1000;

typedef struct _Task_info { // 프로세스 정보 담는 구조체
	char user[10]; // 8글자 이상이면 7번째 문자까지만 표시하고 뒤에 +
	pid_t pid;
	uid_t euid;
	float cpu;
	float mem;
	unsigned long vsz;
	unsigned long rss;
	char tty[20];
	char stat[10];
	char start[10];
	char time[10];
	char default_time[10]; // 아무 옵션 없는 pps명령어에 사용할 시간 format
	char command[128];

//	int is_background_process;
//	int is_terminal_process;
} Task_info;

struct _task_list { // Task_info 배열 관리 구조체
	Task_info **list;
	int len;
	int size;
} Task_list;

void update_uptime(); // uptime 갱신
void print_list(); // 프로세스 정보들 출력
void sort_list_by_pid(); // pid 기준으로 Task_list 정렬하는 함수
int compare_by_pid(const void *a, const void *b); // qsrot에 사용
Task_info *make_new_task_info(pid_t pid); // 새로운 Task_info 생성하는 함수
void append_to_task_list(Task_info* new_info); // Task_list에 원소 추가하는 함수
void init_task_list();
void free_task_list();
char *convert_time_format(unsigned long time); // a, u, x 옵션으로 출력할때 시간 포맷으로 변환하는 함수
char *convert_start_time(unsigned long long time); // 프로세스 시작된 시간 포맷으로 변환하는 함수
char *convert_default_time_format(unsigned long time); // 옵션없이 실행됐을 때 시간 포맷으로 변환하는 함수
void update_task_status(); // Task_list 갱신하는 함수
unsigned long get_current_time(); // 현재시간 밀리초단위로 구해서 리턴하는 함수
void print_u_format(Task_info *t); // u옵션 출력 양식으로 프로세스 정보 하나 출력
void print_a_x_format(Task_info *t); // a, x 옵션 출력 양식으로 프로세스 정보 하나 출력
void print_selected_options();  // 어느 옵션이 주어졌나 출력 (디버깅용)
void print_default_format(Task_info *t);// 아무 옵션 없을 때 양식으로 프로세스 정보 하나 출력

typedef struct _Device_info { // /dev에 있는 파일들 검사해서 여기에 저장해둔다
	char name[MAXNAMLEN + 1];
	unsigned int major_nr;
	unsigned int minor_nr;
} Device_info;

struct _Device_list {
	Device_info **list;
	int len;
	int size;
} Device_list;

void get_devices(); // /dev 디렉토리 확인해서 저장하는 함수
void free_device_list();
void init_device_list();
void append_to_device_list(Device_info* new_info);// 새 원소 추가 함수
void print_device_list();
void get_cur_usr_name(); // 이 프로세스 실행한 유저 이름 구하는 함수

void init_screen();
void check_options(int argc, char *argv[]);

int option_a = 0;
int option_u = 0;
int option_x = 0;

int page_size_in_KiB;
char cur_usr_name[BUFFER_SIZE];
uid_t euid;
unsigned int cp_major; // 현재 프로세스의 tty major number
unsigned int cp_minor; // 현재 프로세스의 tty minor number
char *tty;
int x, y;
char print_buf[1024];


int main(int argc, char *argv[]) {
	int opt;

	tty = ttyname(2);
	if (!strncmp(tty, "/dev/", 5)) {
		tty += 5;
	}
	//printf("%s\n", tty);

	init_screen();
	getmaxyx(stdscr, y, x); // 화면 크기만 알아내고 바로 종료
	endwin();

	page_size_in_KiB = getpagesize() / 1024; // KiB 단위의 페이지 크기 계산
	check_options(argc, argv); // 옵션 확인
	//print_selected_options();

	euid = geteuid();
	get_cur_usr_name(); // 이 프로세스 실행한 유저 이름 가져온다
	update_uptime(); // uptime 구해서 저장
	init_device_list(); // Device_list 초기화
	get_devices(); // /dev 에서 device 목록 가져와 저장
	//print_device_list();
	init_task_list(); // Task_list 초기화
	update_task_status(); // 프로세스 정보들 가져옴

	print_list();

	free_device_list();
	free_task_list();

	return 0;
}

void init_screen() {
	initscr();

	return;
}

void check_options(int argc, char *argv[]) {
	int i, j;

	for (i = 1; i < argc; ++i) {
		int arg_len = strlen(argv[i]);
		for (j = 0; j < arg_len; ++j) {
			char arg_chr = argv[i][j];
			switch(arg_chr) {
				case 'a':
					option_a = 1;
					break;
				case 'u':
					option_u = 1;
					break;
				case 'x':
					option_x = 1;
					break;
				default:
					fprintf(stderr, "error: unsupported option\n");
					endwin();
					exit(1);
			}
		}
	}

	return;
}

void print_selected_options() { // 디버깅용 함수

	printf("selected options: ");
	if (option_a) {
		printf("a");
	}

	if (option_u) {
		printf("u");
	}

	if (option_x) {
		printf("x");
	}
	printf("\n");

	return;
}

////////////////////////////////////////////// task_info
void free_task_list() {
	if (Task_list.list != NULL) {
		int i;
		for (i = 0; i < Task_list.len; ++i) {
			if (Task_list.list[i] != NULL) {
				free(Task_list.list[i]);
			}
		}
		free(Task_list.list);

		Task_list.list = NULL;
		Task_list.len = 0;
		Task_list.size = 0;
	}

	return;
}

void init_task_list() {
	free_task_list();
	Task_list.list = (Task_info **)malloc(INIT_LIST_SIZE * sizeof(Task_info));
	if (Task_list.list == NULL) {
		fprintf(stderr, "malloc error in init_task_list\n");
		endwin();
		exit(1);
	}
	Task_list.len = 0;
	Task_list.size = INIT_LIST_SIZE;

	return;
}

void append_to_task_list(Task_info* new_info) {
	if (Task_list.len == Task_list.size) { // 배열 꽉찼다면
		Task_list.size *= 2; //배열 사이즈 2배로 늘림
		Task_list.list = (Task_info **)realloc(Task_list.list, Task_list.size * sizeof(Task_info *)); 
		if (Task_list.list == NULL) { // 재할당 실패시
			fprintf(stderr, "realloc error in append_to_task_list\n");
			endwin();
			exit(1);
		}
	}
	Task_list.list[(Task_list.len)++] = new_info; // 새로운 원소 추가

	return;
}

Task_info *make_new_task_info(pid_t pid) {
	FILE *fp;
	char fname[MAXNAMLEN + 1];
	char tmp[BUFFER_SIZE];
	const char *mode = "r";
	float mem_total;
	int i;
	struct passwd *result;
	uid_t uid;
	pid_t session, pgrp, tpgid;
	long ni, num_thread, VmLck;
	unsigned long stime, utime;
	unsigned long long starttime;
	unsigned long user, nice, system, idle, lowait, irq, softirq, steal, guest, guest_nice;
	unsigned long cur_cpu_nonidle, cur_cpu_idle, cur_cpu_time;
	char *time_string, *default_time_string;
	dev_t tty_nr;
	unsigned int major_nr, minor_nr;
	char tmp_command[MAXNAMLEN + 1];

	//printf("1111\n");
	Task_info *new_info = (Task_info *)malloc(sizeof(Task_info));
	if (new_info == NULL) {
		fprintf(stderr, "malloc error in make_new_task_info\n");
		endwin();
		exit(1);
	}
	new_info->pid = pid; // pid

	//printf("2222\n");
	/////
	// /proc/[pid]/stat에서 필요한 정보 가져온다
	sprintf(fname, "/proc/%d/stat", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}

	for(i = 0; i < 2; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%s", new_info->stat); // status
	fscanf(fp, "%s", tmp);
	fscanf(fp, "%d", &pgrp); // pgrp
	fscanf(fp, "%d", &session); // sid
	fscanf(fp, "%ld", &tty_nr); // tty_nr
	major_nr = major(tty_nr); // major 번호
	minor_nr = minor(tty_nr); // minor 번호

	int tty_find_flag = 0;
	for(i = 0; i < Device_list.len; ++i) { // 만들어둔 디바이스 리스트에서 일치하는 디바이스를 찾는다
		if (Device_list.list[i]->major_nr == major_nr) {
			if (Device_list.list[i]->minor_nr == minor_nr) {
				tty_find_flag = 1;
				strncpy(new_info->tty, Device_list.list[i]->name, 18);
				new_info->tty[19] = '\0';
			}
		}
		//printf("ma: %d ,mi: %d, ma2: %d, mi2: %d\n", Device_list.list[i]->major_nr, Device_list.list[i]->minor_nr, major_nr, minor_nr);
	}
	if (!tty_find_flag) {// 일치하는 걸 찾지 못했을 때
		strcpy(new_info->tty, "?");
	}

	fscanf(fp, "%d", &tpgid);
	for(i = 0; i < 5; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%lu%lu", &utime, &stime); // time
	time_string = convert_time_format(stime + utime); // a, u, x 옵션일 때 출력할 시간 문자열
	default_time_string = convert_default_time_format(stime + utime); // 아무 옵션 없을 때 출력할 시간 문자열
	strcpy(new_info->time, time_string);
	strcpy(new_info->default_time, default_time_string);
	free(time_string);
	free(default_time_string);
	for(i = 0; i < 3; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%ld", &ni); // ni
	fscanf(fp, "%ld", &num_thread); // num_thread
	fscanf(fp, "%s", tmp);
	fscanf(fp, "%llu", &starttime); // starttime
	fclose(fp);
	
	//printf("3333\n");
	/////
	// /proc/[pid]/loginuid에서 필요한 정보 가져온다
	sprintf(fname, "/proc/%d/loginuid", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fscanf(fp, "%d", &uid);
	fclose(fp);

	if ((result = getpwuid(uid)) == NULL) { // 위에서 가져온 uid 이용해 username 알아낸다
		strcpy(new_info->user, "root");
	} else {
		if (strlen(result->pw_name) > 7) {
			strncpy(new_info->user, result->pw_name, 7);
			new_info->user[7] = '+';
			new_info->user[8] = '\0';
		} else {
			strcpy(new_info->user, result->pw_name);
		}
	} // USER

	//printf("4444\n");
	/////
	// /proc/[pid]/status에서 필요한 정보 가져온다
	sprintf(fname, "/proc/%d/status", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fscanf(fp, "%s%s", tmp, tmp_command); // COMMAND
	tmp_command[15] = '\0';
	sprintf(new_info->command, "[%s]", tmp_command);
	strcpy(tmp_command, "");
	for (i = 0; i < 8; ++i)
		fgets(tmp, BUFFER_SIZE, fp);
	fscanf(fp, "%s%s%d%s%s", tmp, tmp, &(new_info->euid), tmp, tmp); // euid
	for (i = 0; i < 10; ++i)
		fgets(tmp, BUFFER_SIZE, fp);
	fscanf(fp, "%s%ld", tmp, &VmLck); // VmLck
	if (strcmp(tmp, "VmLck:")) { // /proc/[pid]/status에 VmLck 정보가 없는경우
		VmLck = 0;
	}

	fclose(fp);

	//printf("5555\n");
	/////
	// /proc/[pid]/statm에서 필요한 정보 가져온다
	sprintf(fname, "/proc/%d/statm", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fscanf(fp, "%lu%lu", &(new_info->vsz), &(new_info->rss));
	fclose(fp);
	new_info->vsz *= page_size_in_KiB; // virt
	new_info->rss *= page_size_in_KiB; // rss

	// cmdline
	// /proc/[pid]/cmdline에서 필요한 정보 가져온다
	sprintf(fname, "/proc/%d/cmdline", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fgets(tmp_command, 1024, fp); // mem
	fclose(fp);
	if (strcmp(tmp_command, "")) { // cmdline에서 아무것도 가져오지 못한 경우에는 status에서 읽은 command로 대체한다
		strcpy(new_info->command, tmp_command);
	}

	//printf("6666\n");
	/////
	// /proc/meminfo에서 필요한 정보 가져온다
	sprintf(fname, "/proc/meminfo");
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fscanf(fp, "%s%f", tmp, &mem_total); // mem
	fclose(fp);

	new_info->mem = (new_info->rss) / mem_total * 100.0;

	// status 상세 정보 추가
	if (ni > 0) {
		strcat(new_info->stat, "N");
	} else if (ni < 0) {
		strcat(new_info->stat, "<");
	}

	if (VmLck > 0) {
		strcat(new_info->stat, "L"); 
	}

	if (pid == session) {
		strcat(new_info->stat, "s");
	}

	if (num_thread > 1) {
		strcat(new_info->stat, "l");
	}

	if (pgrp == tpgid) {
		strcat(new_info->stat, "+");
	}

	// cpu 계산
	sprintf(fname, "/proc/stat");
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		endwin();
		exit(1);
	}
	fscanf(fp, "%s%lu%lu%lu%lu%lu%lu%lu%lu%lu%lu", tmp, &user, &nice, &system, &idle, &lowait, &irq, &softirq, &steal, &guest, &guest_nice);
	fclose(fp);
	cur_cpu_nonidle = user + nice + system + irq + softirq + steal;
	cur_cpu_idle = idle + lowait;
	cur_cpu_time = cur_cpu_nonidle + cur_cpu_idle;
	new_info->cpu = ((float)(stime + utime) / (uptime * sysconf(_SC_CLK_TCK) - starttime)) * 100.0; 

	// start 계산
	time_string = convert_start_time(starttime);
	strcpy(new_info->start, time_string);
	free(time_string);


	return new_info;
}

int compare_by_pid(const void *a, const void *b) {
	return (*(Task_info **)a)->pid - (*(Task_info **)b)->pid;
}

void sort_list_by_pid() {
	qsort(Task_list.list, Task_list.len, sizeof(Task_info *), compare_by_pid);
}

void print_list() { // 프로세스 정보들을 출력한다
	int i;
	char ts[1024];

	// 각 항목 이름 출력
	if (option_u) {
		sprintf(ts, "%-8s%5s%5s%5s %7s %6s %-7s %-4s%6s%8s %s", "USER", "PID", "%CPU", "%MEM", "VSZ", "RSS", "TTY", "STAT", "START", "TIME", "COMMAND");
		strncpy(print_buf, ts, x);
		print_buf[x] = '\0';
		printf("%s", print_buf);
	} else if (option_a || option_x) {
		sprintf(ts, "%5s %-7s %-4s%8s %s", "PID", "TTY", "STAT", "TIME", "COMMAND");
		strncpy(print_buf, ts, x);
		print_buf[x] = '\0';
		printf("%s", print_buf);

	} else {
		sprintf(ts, "%5s %-7s %8s %s", "PID", "TTY", "TIME", "CMD");
		strncpy(print_buf, ts, x);
		print_buf[x] = '\0';
		printf("%s", print_buf);
	}

	// 프로세스 정보들 출력
	for (i = 0; i < Task_list.len; ++i) {
		if(option_a && !option_x) {
			if (strcmp(Task_list.list[i]->tty, "?")) {
				if (option_u) {
					print_u_format(Task_list.list[i]);
				} else {
					print_a_x_format(Task_list.list[i]);
				}
			}
		} else if (!option_a && option_x) {
			if (!strcmp(Task_list.list[i]->user, cur_usr_name)) {
				if (option_u) {
					print_u_format(Task_list.list[i]);
				} else {
					print_a_x_format(Task_list.list[i]);
				}
			}
		} else if (option_a && option_x) {
			if (option_u) {
				print_u_format(Task_list.list[i]);
			} else {
				print_a_x_format(Task_list.list[i]);
			}
		} else {
			if (option_u) {
				if (strcmp(Task_list.list[i]->tty, "?")) {
					if (!strcmp(Task_list.list[i]->user, cur_usr_name)) {
						print_u_format(Task_list.list[i]);
					}
				}

			} else {
				if (Task_list.list[i]->euid == euid) {
					if (!strcmp(tty, Task_list.list[i]->tty)) {
						print_default_format(Task_list.list[i]);
					}
				}
			}
		}
	}
	printf("\n");

	return;
}

char *convert_time_format(unsigned long time){
	char *time_string;
	unsigned long tmp_seconds = time / sysconf(_SC_CLK_TCK);
	unsigned long minutes = tmp_seconds / 60;
	int seconds = (time - minutes * 60 * sysconf(_SC_CLK_TCK)) / sysconf(_SC_CLK_TCK);
	time_string = (char *)malloc(16 * sizeof(char));

	sprintf(time_string, "%lu:%02d", minutes, seconds);
	return time_string;
}

char *convert_start_time(unsigned long long starttime){
	time_t cur_time = time(NULL); 
	time_t running_time;
	time_t start_time;
	char *time_string;
	char *start_time_str;

	running_time = (time_t)(uptime - (starttime / sysconf(_SC_CLK_TCK)));
//	printf("runningtime : %ld\n", running_time);
//	printf("uptime : %ld\n", uptime);
//	printf("start time:%ld\n", starttime / sysconf(_SC_CLK_TCK));

	start_time = cur_time - running_time;
	start_time_str = ctime(&start_time);
	time_string = (char *)malloc(16 * sizeof(char));
	strncpy(time_string, start_time_str + 11, 5);
	time_string[5] = '\0';

	return time_string;
}

char *convert_default_time_format(unsigned long time) {
	char *time_string;
	unsigned long tmp_seconds = time / sysconf(_SC_CLK_TCK);
	unsigned long minutes = tmp_seconds / 60;
	unsigned long hours = minutes / 60;
	minutes -= hours * 60;
	int seconds = (time - minutes * 60 * sysconf(_SC_CLK_TCK)) / sysconf(_SC_CLK_TCK);
	time_string = (char *)malloc(16 * sizeof(char));

	sprintf(time_string, "%02lu:%02lu:%02d", hours, minutes, seconds);
	return time_string;
}

void update_uptime() {
	FILE *fp;
        const char* fname = "/proc/uptime";
        const char* mode = "r";
        float fuptime;

        if ((fp = fopen(fname, mode)) == NULL) {
                fprintf(stderr, "fopen error for %s\n", fname);
                endwin();
                exit(1);
        }
        fscanf(fp, "%f", &fuptime);
	uptime = (unsigned long)fuptime;

	return;
}

void update_task_status() {
	struct dirent *dentry;
	struct stat statbuf;
	char filename[MAXNAMLEN + 1];
	DIR *dirp;
	int i;

	if ((dirp = opendir("/proc")) == NULL) {
		fprintf(stderr, "opendir error for /proc\n");
		endwin();
		exit(1);
	}

	//printf("1111\n");

	while ((dentry = readdir(dirp)) != NULL) { // /proc 내의 모든 파일 확인
		//printf("2222\n");
		pid_t pid;
		if (dentry->d_ino == 0)
			continue;

		memcpy(filename, dentry->d_name, MAXNAMLEN);
		if ((pid = atoi(filename))) { // 파일 이름이 숫자라면
			Task_info *new_info;

			new_info = make_new_task_info(pid); // 해당 파일 이름으로 새로운 Task_info 만든다
			append_to_task_list(new_info); // Task_list에 새로운 원소 추가
		} else {
			continue;
		}
	}

	sort_list_by_pid();
	//printf("8888\n");
	return;
}

unsigned long get_current_time() {
        struct timespec ts;

        if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                fprintf(stderr, "clock_gettime error\n");
                endwin();
                exit(1);
        }

        return (NANOS * ts.tv_sec + ts.tv_nsec) / (NANOS / MILLIS);
}

void print_u_format(Task_info *t) {
	char ts[1024];
	sprintf(ts, "\n%-8s%5d%5.1f%5.1f %7lu %6lu %-7s %-4s%6s%8s %s", t->user, t->pid, t->cpu, t->mem, t->vsz, t->rss, t->tty, t->stat, t->start, t->time, t->command);
	strncpy(print_buf, ts, x);
	print_buf[x] = '\0';
	printf("%s", print_buf);

	return;
}

void print_a_x_format(Task_info *t) {
	char ts[1024];
	sprintf(ts, "\n%5d %-7s %-4s%8s %s", t->pid, t->tty, t->stat, t->time, t->command);
	strncpy(print_buf, ts, x);
	print_buf[x] = '\0';
	printf("%s", print_buf);

	return;
}

void print_default_format(Task_info *t) {
	char ts[1024];
	sprintf(ts, "\n%5d %-7s %8s %s", t->pid, t->tty, t->default_time, t->command);
	strncpy(print_buf, ts, x);
	print_buf[x] = '\0';
	printf("%s", print_buf);

	return;
}

void get_devices() { // /dev에 있는 디바이스 파일들 확인해서 저장한다
	struct dirent *dentry;
	struct stat statbuf;
	char filename[MAXNAMLEN + 128];
	DIR *dirp;
	int i;

	if ((dirp = opendir("/dev")) == NULL) {
		fprintf(stderr, "opendir error for /dev\n");
		endwin();
		exit(1);
	}

	//printf("1111\n");

	while ((dentry = readdir(dirp)) != NULL) {
		if (dentry->d_ino == 0)
			continue;

		if (!strcmp(dentry->d_name, ".") || !strcmp(dentry->d_name, ".."))
			continue;


		sprintf(filename, "/dev/%s", dentry->d_name);
		//printf("%s\n", filename);
		if (stat(filename, &statbuf) == -1) {
			fprintf(stderr, "stat error for %s\n", filename);
			break;
		}

		if (S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode))
			continue;

		if (S_ISCHR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) { // 해당 파일의 major, minor number 저장한다
			Device_info *new_info = (Device_info *)malloc(sizeof(Device_info));
			strcpy(new_info->name, filename + 5);
			new_info->major_nr = major(statbuf.st_rdev);
			new_info->minor_nr = minor(statbuf.st_rdev);

			append_to_device_list(new_info);
		}
	}

	closedir(dirp);

	// opendir /dev/pts
	if ((dirp = opendir("/dev/pts")) == NULL) { // /dev/pts 디렉토리는 따로 확인한다
		fprintf(stderr, "opendir error for /dev/pts\n");
		endwin();
		exit(1);
	}

	//printf("1111\n");
	while ((dentry = readdir(dirp)) != NULL) {
		//printf("2222\n");
		pid_t pid;
		if (dentry->d_ino == 0)
			continue;

		if (!strcmp(dentry->d_name, ".") || !strcmp(dentry->d_name, ".."))
			continue;


		sprintf(filename, "/dev/pts/%s", dentry->d_name);
		if (stat(filename, &statbuf) == -1) {
			fprintf(stderr, "stat error for %s\n", filename);
			break;
		}

		if (S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode))
			continue;

		if (S_ISCHR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) {
			Device_info *new_info = (Device_info *)malloc(sizeof(Device_info));
			strcpy(new_info->name, filename + 5);
			new_info->major_nr = major(statbuf.st_rdev);
			new_info->minor_nr = minor(statbuf.st_rdev);

			append_to_device_list(new_info);
		}
	}

	closedir(dirp);

	return;
}

void free_device_list() {
	if (Device_list.list != NULL) {
		int i;
		for (i = 0; i < Device_list.len; ++i) {
			if (Device_list.list[i] != NULL) {
				free(Device_list.list[i]);
			}
		}
		free(Device_list.list);

		Device_list.list = NULL;
		Device_list.len = 0;
		Device_list.size = 0;
	}

	return;
}

void init_device_list() {
	free_device_list();
	Device_list.list = (Device_info **)malloc(INIT_LIST_SIZE * sizeof(Device_info));
	if (Device_list.list == NULL) {
		fprintf(stderr, "malloc error in init_device_list\n");
		endwin();
		exit(1);
	}
	Device_list.len = 0;
	Device_list.size = INIT_LIST_SIZE;

	return;
}

void append_to_device_list(Device_info* new_info) {
	if (Device_list.len == Device_list.size) { // 배열 꽉찼으면 
		Device_list.size *= 2; // 사이즈 두배로 늘림
		Device_list.list = (Device_info **)realloc(Device_list.list, Device_list.size * sizeof(Device_info *)); // 재할당
		if (Device_list.list == NULL) { // 재할당 실패시
			fprintf(stderr, "realloc error in append_to_device_list\n");
			endwin();
			exit(1);
		}
	}
	Device_list.list[(Device_list.len)++] = new_info;

	return;
}

void print_device_list() {
	int i;

	for (i = 0; i < Device_list.len; ++i) {
		printf("%s, ma: %d, mi: %d\n", Device_list.list[i]->name, Device_list.list[i]->major_nr, Device_list.list[i]->minor_nr);
	}

	return;
}

void get_cur_usr_name() {
	struct passwd *pwd;

	if ((pwd = getpwuid(getuid())) == NULL) {
		fprintf(stderr, "getpwuid error\n");
		endwin();
		exit(1);
	}
	strcpy(cur_usr_name, pwd->pw_name);

	return;
}
