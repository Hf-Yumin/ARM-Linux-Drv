#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>

int main(int argc, char **argv)
{
	int fd;
	unsigned char key_val;
	int ret;
	
	fd = open("/dev/buttons", O_RDWR);
	if(fd < 0)
		{
			printf("Can't open!\n");
			return -1;
		}
	
	while(1)
	{
		ret = read(fd, &key_val, 1);
		printf("key_val = 0x%x, ret = %d\n", key_val, ret);
	}
	
	return 0;
}
