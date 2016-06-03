#include <stdio.h>
#include <string.h>

/* implement these funcs in assembly code */
extern int mywrite(int fd, char *buf, int count);
extern int myadd(int a, int b, int *res);

int main()
{
	char buf[1024];
	int a, b, res;
	char *mystr = "Calculating...\n";
	char *emsg = "Error in adding\n";
	char *end = "Program end\n";

	a = 5;
	b = 10;
	mywrite(1, mystr, strlen(mystr));
	if (myadd(a, b, &res)) {
		sprintf(buf, "The result is %d\n", res);
		/* following line can not output, maybe need to familiar the
		 * x86-64 calling convention */
		mywrite(1, buf, strlen(buf));
	} else {
		mywrite(1, emsg, strlen(emsg));
	}
	mywrite(1, end, strlen(end));

	return 0;
}
