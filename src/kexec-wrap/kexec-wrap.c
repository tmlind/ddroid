#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#define KMSG	"/dev/kmsg"
#define UART	"/dev/ttyO2"

static const char *opt_string = "h";

static const struct option long_opts[] = {
	{ "help", no_argument, NULL, 'h' },
	{ NULL, no_argument, NULL, 0 }
};

static int fd_kmsg, fd_uart;
static char **args;
static int found;

void info(const char *fmt, ...)
{
	va_list arglist;

	va_start(arglist, fmt);
	vfprintf(stdout, fmt, arglist);
	if (fd_kmsg > 0) {
		dprintf(fd_kmsg, "<1>");
		vdprintf(fd_kmsg, fmt, arglist);
	}
	if (fd_uart > 0) {
		vdprintf(fd_uart, fmt, arglist);
		dprintf(fd_uart, "\r");
	}
	va_end(arglist);
}

void err(const char *fmt, ...)
{
	va_list arglist;

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	if (fd_kmsg > 0) {
		dprintf(fd_kmsg, "<1>ddroid: ");
		vdprintf(fd_kmsg, fmt, arglist);
	}
	if (fd_uart > 0) {
		vdprintf(fd_uart, fmt, arglist);
		dprintf(fd_uart, "\r");
	}
	va_end(arglist);
}

static void print_help(void)
{
	err("Please add kexec options to /system/etc/kexec-wrap.conf\n\n");
}

static int parse_cfg(const char *name)
{
	FILE *fp;
	char *line = NULL;
	int error = 0;
	size_t len;
	ssize_t read;

	if (!(fp = fopen(name, "r"))) {
		err("Could not open conf file %s\n", name);

		return -ENXIO;
	}

	args = malloc(sizeof(char *) * 128);
	if (!args) {
		error = -ENOMEM;
		goto close;
	}
	memset(args, '\0', sizeof(*args));

	/* The logwrapper script copies kexec files to root first */
	args[found] = malloc(strlen("/kexec.static"));
	if (!args[found]) {
		error = -ENOMEM;
		goto close;
	}
	memset(args[found], '\0', sizeof(*args[found]));
	strcpy(args[found], "/kexec.static");
	found++;

	while ((read = getline(&line, &len, fp)) != -1) {
		if (read <= 1)
			continue;
		if (!strncmp("#", line, 1) || !strncmp(";", line, 1))
			continue;

		args[found] = malloc(strlen(line) + 2);
		if (!args[found]) {
			error = -ENOMEM;
			goto close;
		}
		memset(args[found], '\0', sizeof(*args[found]));

		if (strncmp("-", line, 1) && strncmp("/", line, 1))
			strcpy(args[found], "--");

		strncat(args[found], line, strlen(line) - 1);
		found++;
	}
	free(line);

close:
	fclose(fp);

	return error;
}

extern void wait(int *);
#define MAX_ARG	128
static int run_command(char *cmd, ...)
{
	va_list arguments;
	char **a;
	int i = 0, pid;

	a = malloc(sizeof(char *) * MAX_ARG);
	if (!a)
		return -ENOMEM;
	memset(a, '\0', sizeof(*a));

	a[i++] = "/sbin/bbx";
	a[i++] = cmd;
	fprintf(stdout, "%s %s\n", a[0], a[1]);

	va_start(arguments, cmd);
	while (1) {
		a[i] = va_arg(arguments, char *);
		if (!a[i])
			break;
		i++;
	}
	va_end(arguments);

	pid = fork();
	if (pid < 0) {
		err("Could not fork\n");
		return pid;
	} else if (pid == 0){
		char buf[1024];
		char *bufp = buf;
		int error;

		bufp += sprintf(bufp, "Running command: ");
		for (i = 0; i < MAX_ARG; i++) {
			if (!a[i])
				break;
			bufp += sprintf(bufp, "%s ", a[i]);
		}
		bufp += sprintf(bufp, "\n");
		info(buf);
		error = execvp(a[0], a);
		if (error) {
			err("Could not remount rw: %i\n", error);

			return error;
		}
		exit(0);
	} else {
		int error;

		/* Parent */
		wait(&error);

		return error;
	}

	return -1;
}

static int run_kexec(char *const argv[])
{
	int pid;

	pid = fork();
	if (pid < 0) {
		err("Could not fork\n");

		return pid;
	} else if (pid == 0){
		int error;

		error = execvp(argv[0], argv);
		if (error)
			return error;
		exit(0);
	} else {
		int error;

		/* Parent */
		wait(&error);

		return error;
	}

	return -1;
}

int initialize_kmsg(void)
{
	int error = 0;

	fd_kmsg = open(KMSG, O_RDWR | O_NONBLOCK);
	if (fd_kmsg < 0) {
		fprintf(stderr, "Could not open kmsg: %i\n", fd_kmsg);
		error = fd_kmsg;
	}

	return error;
}

int initialize_uart(void)
{
	struct termios tio;
	struct stat s;
	int error = 0;

	if (stat(UART, &s) != 0) {
		error = run_command("mount", "-o", "rw,remount", "/", NULL);
		if (error) {
			fprintf(stderr, "Could not remount: %i\n", error);
			return error;
		}

		error = run_command("mknod", UART, "c", "249", "2", NULL);
		if (error)
			fprintf(stderr, "Could not mknod: %i\n", error);

		error = run_command("mount", "-o", "ro,remount", "/", NULL);
		if (error)
			fprintf(stderr, "Could not remount rw: %i\n", error);
	}

	fd_uart = open(UART, O_RDWR | O_NONBLOCK);
	if (fd_uart < 0) {
		fprintf(stderr, "Could not open UART: %i\n", fd_uart);
		error = fd_uart;
	}

	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = CS8 | CREAD | CLOCAL;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 5;
	cfsetospeed(&tio, B115200);
	cfsetispeed(&tio, B115200);
	tcsetattr(fd_uart, TCSANOW, &tio);

	return error;
}

int set_permissions(void)
{
	int error;

	error = run_command("mount", "-o", "rw,remount", "/", NULL);
	if (error) {
		err("Could not remount: %i\n", error);

		return error;
	}

	error = run_command("chmod", "755", "/kexec.static", NULL);
	if (error)
		err("Could not make executable: %i\n", error);

	error = run_command("chown", "0.2000", "/kexec.static", NULL);
	if (error)
		err("Could not change owner: %i\n", error);

	error = run_command("mount", "-o", "ro,remount", "/", NULL);
	if (error)
		err("Could not remount rw: %i\n", error);

	return error;
}

int main(int argc, char **argv)
{
	int opt, error, i;
	char buf[1024];
	char *bufp = buf;

	/* Ignore unrecognized command line options */
	opterr = 0;

	while ((opt = getopt_long(argc, argv, opt_string, long_opts, 0)) != -1) {
		switch (opt) {
		case 'h':
			print_help();
			return EXIT_FAILURE;
		default:
			break;
		}
	}

	info("Initalizing kmsg...\n");
	error = initialize_kmsg();
	if (error)
		err("Could not initialize kmsg\n");

	info ("Initializing uart..\n");
	error = initialize_uart();
	if (error)
		err("Could not initialize UART\n");

	info("Setting permissions..\n");
	error = set_permissions();
	if (error)
		goto free;

	/* Our kexec-wrap.conf gets copied to root by logwrapper */
	info("Parsing /kexec-wrap.conf for kexec options\n");
	error = parse_cfg("/kexec-wrap.conf");
	if (error)
		goto free;

	args[found + 1] = NULL;

	bufp += sprintf(bufp, "Loading kexec kernel with: ");
	for (i = 0; i < found; i++) {
		bufp += sprintf(bufp, "%s ", args[i]);
	}
	bufp += sprintf(bufp, "\n");
	info(buf);

	/* Load kernel, dtb and initramfs */
	error = run_kexec(args);
	if (error) {
		err("Could not load kexec kernel: %i\n", error);
		goto free;
	}

	/* Bye Bye */
	info("Starting new kernel with kexec -e\n");
	args[1] = "-e";
	args[2] = NULL;
	error = run_kexec(args);
	if (error)
		err("Could not kexec: %i\n", error);

	/* Something went wrong, remount /system to allow adb to connect */
	error = run_command("mount", "-t", "ext3", "/dev/block/loop-system", "/system", NULL);
	if (error) {
		err("Could not remount: %i\n", error);

		return error;
	}

free:
	while (found--)
		free(args[found]);
	free(args);
	if (fd_uart > 0)
		close(fd_uart);

	if (fd_kmsg > 0)
		close(fd_kmsg);

	return error;
}
