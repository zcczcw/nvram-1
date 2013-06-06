#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "nvram.h"
#include "nvram_env.h"
#include "flash_api.h"

#include <linux/autoconf.h>

char libnvram_debug = 0;
#define LIBNV_PRINT(x, ...) do { if (libnvram_debug) printf("%s %d: " x, __FILE__, __LINE__, ## __VA_ARGS__); } while(0)
#define LIBNV_ERROR(x, ...) do { printf("%s %d: ERROR! " x, __FILE__, __LINE__, ## __VA_ARGS__); } while(0)

static block_t fb[FLASH_BLOCK_NUM] =
{
	{
		.name = "2860",
		.flash_offset =  0x0,
		.flash_max_len = 0x8000,
		.valid = 0
	}
};


//x is the value returned if the check failed
#define LIBNV_CHECK_INDEX(x) do { \
	if (index < 0 || index >= FLASH_BLOCK_NUM) { \
		LIBNV_PRINT("index(%d) is out of range\n", index); \
		return x; \
	} \
} while (0)

#define LIBNV_CHECK_VALID(nvramfd_addr) do { \
	if (!fb[index].valid ) { \
		LIBNV_PRINT("fb[%d] invalid, init again\n", index); \
		nvram_init_ralink(index,nvramfd_addr); \
	} \
} while (0)

#define FREE(x) do { if (x != NULL) {free(x); x=NULL;} } while(0)


/*
 * 1. read env from flash
 * 2. parse entries
 * 3. save the entries to cache
 */
int nvram_init_ralink(int index, int * nvram_fd) 
{
	unsigned long from;
	int i, len;
	char *p, *q;
	char tmp_test[500];
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#endif
	int ret;
	LIBNV_PRINT("--> nvram_init %d\n", index);
	LIBNV_CHECK_INDEX();
	if (fb[index].valid)
		return 0;
	
#ifdef CONFIG_KERNEL_NVRAM
	/*
	 * read data from flash
	 * skip crc checking which is done by Kernel NVRAM module 
	 */
	from = fb[index].flash_offset + sizeof(fb[index].env.crc);
	len = fb[index].flash_max_len - sizeof(fb[index].env.crc);
	fb[index].env.data = (char *)malloc(len);
	nvr.index = index;
	nvr.value = fb[index].env.data;

	//printf("======from %lx len %x index %d=====\n",from, len, nvr.index);
       
	//fd = open(NV_DEV, O_RDONLY);
	*nvram_fd = fd = open(NV_DEV, O_RDONLY);
	//here maybe not useful because write is use ioctl in the below set also open the NV_DEV with 	O_RDONLY ,commented by daniel
	//printf("=========*nvram_fd %d= fd=%d===========\n", *nvram_fd,fd);
	if (fd < 0) {
		perror(NV_DEV);
		ret=fd;
		goto out;
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_GETALL, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		ret=fd;
		goto out;
	}
	close(fd);
#else
	//read crc from flash
	from = fb[index].flash_offset;
	len = sizeof(fb[index].env.crc);
	flash_read((char *)&fb[index].env.crc, from, len);

	//read data from flash
	from = from + len;
	len = fb[index].flash_max_len - len;
	fb[index].env.data = (char *)malloc(len);
	flash_read(fb[index].env.data, from, len);

	//check crc
	//printf("crc shall be %08lx\n", crc32(0, (unsigned char *)fb[index].env.data, len));
	if (crc32(0, (unsigned char *)fb[index].env.data, len) != fb[index].env.crc) {
		LIBNV_PRINT("Bad CRC %x, ignore values in flash.\n", fb[index].env.crc);
		FREE(fb[index].env.data);
		//empty cache
		fb[index].valid = 1;
		fb[index].dirty = 0;
		return;
	}
#endif

	//parse env to cache
	p = fb[index].env.data;
	for (i = 0; i < MAX_CACHE_ENTRY; i++) {
		if (NULL == (q = strchr(p, '='))) {
			LIBNV_PRINT("parsed failed - cannot find '='\n");
			break;
		}
		*q = '\0'; //strip '='
		fb[index].cache[i].name = strdup(p);
		//printf("  %d '%s'->", i, p);

		p = q + 1; //value
		if (NULL == (q = strchr(p, '\0'))) {
			LIBNV_PRINT("parsed failed - cannot find '\\0'\n");
			break;
		}
		fb[index].cache[i].value = strdup(p);
		//printf("'%s'\n", p);

		p = q + 1; //next entry
		if (p - fb[index].env.data + 1 >= len) //end of block
			break;
		if (*p == '\0') //end of env
			break;
	}
	if (i == MAX_CACHE_ENTRY)
		LIBNV_PRINT("run out of env cache, please increase MAX_CACHE_ENTRY\n");

	fb[index].valid = 1;
	fb[index].dirty = 0;
	//LIBNV_PRINT("==init====valid=%d=====\n", fb[index].valid);
	return 0;

out:
	FREE(fb[index].env.data); //free it to save momery
	return ret;
}

void nvram_close_ralink(int index)
{
	int i;

	LIBNV_PRINT("--> nvram_close %d\n", index);
	LIBNV_CHECK_INDEX();

	if (!fb[index].valid)
		return;
	//if (fb[index].dirty)
		//nvram_commit_ralink(index);

	//free env
	FREE(fb[index].env.data);

	//free cache
	for (i = 0; i < MAX_CACHE_ENTRY; i++) {
		FREE(fb[index].cache[i].name);
		FREE(fb[index].cache[i].value);
	}

	fb[index].valid = 0;
}

/*
 * return idx (0 ~ iMAX_CACHE_ENTRY)
 * return -1 if no such value or empty cache
 */
static int cache_idx(int index, char *name)
{
	int i;

	for (i = 0; i < MAX_CACHE_ENTRY; i++) {
		if (!fb[index].cache[i].name)
			return -1;
		if (!strcmp(name, fb[index].cache[i].name))
			return i;
	}
	return -1;
}


char *nvram_get_ralink(int index, const char *name)
{
	//LIBNV_PRINT("--> nvram_get\n");

#ifdef CONFIG_KERNEL_NVRAM
	/* Get the fresh value from Kernel NVRAM moduel,
	 * so there is no need to do nvram_close() and nvram_init() again
	 */
	//nvram_close(index);
	//nvram_init(index);
#else
	nvram_close_ralink(index);
	nvram_init_ralink(index,&tmpfd);
#endif

	return nvram_bufget(index, name);
}



int nvram_getall_ralink(int index, char *buf,int count)
{

	unsigned long from;
	int i, len;
	char *p, *q;
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#endif
	int ret;
	LIBNV_PRINT("--> nvram_init %d\n", index);
	LIBNV_CHECK_INDEX();
	
	/*
	 * read data from flash
	 * skip crc checking which is done by Kernel NVRAM module 
	 */
	from = fb[index].flash_offset ;
	len = fb[index].flash_max_len;
	
	if (count <=len)
		len =count;
	
	
	fb[index].env.data = (char *)malloc(len);
	nvr.index = index;
	nvr.value = fb[index].env.data;

       
 	fd = open(NV_DEV, O_RDONLY);
	 //here maybe not useful because write is use ioctl in the below set also open the NV_DEV with 	O_RDONLY ,commented by daniel
	if (fd < 0) {
		perror(NV_DEV);
		ret=fd;
		goto out;
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_GETALL, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		ret=fd;
		goto out;
	}
	close(fd);

	
	memcpy(buf,nvr.value,len);

out:
	FREE(fb[index].env.data); //free it to save momery
	return len;

}


int nvram_set_ralink(int index, char *name, char *value)
{
	//LIBNV_PRINT("--> nvram_set\n");

	if (-1 == nvram_bufset(index, name, value))

		return -1;
	return 0;
	//return nvram_commit_ralink(index);
}

char  *nvram_bufget(int index, char *name)
{
	int idx;
	static char  *ret;
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#endif
	//add by daniel
	int tmpfd=-1;
	//LIBNV_PRINT("--> nvram_bufget %d\n", index);
	LIBNV_CHECK_INDEX("");
	LIBNV_CHECK_VALID(&tmpfd);

#ifdef CONFIG_KERNEL_NVRAM
	nvr.index = index;
	nvr.name = name;
	nvr.value = malloc(MAX_VALUE_LEN);
	fd = open(NV_DEV, O_RDONLY);
	if (fd < 0) {
		perror(NV_DEV);
		FREE(nvr.value);
		return "";
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_GET, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		FREE(nvr.value);
		return "";
	}
	close(fd);
#endif

	idx = cache_idx(index, name);

	if (-1 != idx) {
		if (fb[index].cache[idx].value) {
			//duplicate the value in case caller modify it
			//ret = strdup(fb[index].cache[idx].value);
#ifdef CONFIG_KERNEL_NVRAM
			FREE(fb[index].cache[idx].value);
			fb[index].cache[idx].value = strdup(nvr.value);
			FREE(nvr.value);
#endif
			ret = fb[index].cache[idx].value;
			LIBNV_PRINT("bufget %d '%s'->'%s'\n", index, name, ret);
			return ret;
		}
	}

	//no default value set?
	//btw, we don't return NULL anymore!
	LIBNV_PRINT("bufget %d '%s'->''(empty) Warning!\n", index, name);

#ifdef CONFIG_KERNEL_NVRAM
	FREE(nvr.value);
#endif
     
	return "";
}


int nvram_bufset(int index, char *name, char *value)
{
	int idx;
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#endif

	//LIBNV_PRINT("--> nvram_bufset\n");
	int tmpfd=-1;
	LIBNV_CHECK_INDEX(-1);
	LIBNV_CHECK_VALID(&tmpfd);

#ifdef CONFIG_KERNEL_NVRAM
	nvr.index = index;
	nvr.name = name;
	nvr.value = value;
	fd = open(NV_DEV, O_RDONLY);
	if (fd < 0) {
		perror(NV_DEV);
		return -1;
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_SET, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
#endif

	idx = cache_idx(index, name);

	if (-1 == idx) {
		//find the first empty room
		for (idx = 0; idx < MAX_CACHE_ENTRY; idx++) {
			if (!fb[index].cache[idx].name)
				break;
		}
		//no any empty room
		if (idx == MAX_CACHE_ENTRY) {
			LIBNV_ERROR("run out of env cache, please increase MAX_CACHE_ENTRY\n");
			return -1;
		}
		fb[index].cache[idx].name = strdup(name);
		fb[index].cache[idx].value = strdup(value);
	}
	else {
		//abandon the previous value
		FREE(fb[index].cache[idx].value);
		fb[index].cache[idx].value = strdup(value);
	}
	LIBNV_PRINT("bufset %d '%s'->'%s'\n", index, name, value);
	fb[index].dirty = 1;

	//LIBNV_PRINT("==set====fb %p  value %p valid=%d=====\n", fb, &(fb[index].valid),fb[index].valid);
	return 0;
}

void nvram_buflist(int index)
{
	int i;

	int  tmpfd=-1;
	
	//LIBNV_PRINT("--> nvram_buflist %d\n", index);
	LIBNV_CHECK_INDEX();
	LIBNV_CHECK_VALID(&tmpfd);

	for (i = 0; i < MAX_CACHE_ENTRY; i++) {
		if (!fb[index].cache[i].name)
			break;
		printf("  '%s'='%s'\n", fb[index].cache[i].name, fb[index].cache[i].value);
	}
}

/*
 * write flash from cache
 */
int nvram_commit_ralink(int index)
{
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#else
	unsigned long to;
	int i, len;
	char *p;
#endif
	//add by daniel 
	int tmpfd=-1;

	//LIBNV_PRINT("--> nvram_commit %d\n", index);
	LIBNV_CHECK_INDEX(-1);
	//LIBNV_PRINT("==commit====fb %p  value addr %pvalid=%d=====\n", fb, &(fb[index].valid),fb[index].valid);
	LIBNV_CHECK_VALID(&tmpfd);


#ifdef CONFIG_KERNEL_NVRAM
	nvr.index = index;
	fd = open(NV_DEV, O_RDWR);
	if (fd < 0) {
		perror(NV_DEV);
		return -1;
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_COMMIT, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
#else
	if (!fb[index].dirty) {
		LIBNV_PRINT("nothing to be committed\n");
		return 0;
	}

	//construct env block
	len = fb[index].flash_max_len - sizeof(fb[index].env.crc);
	fb[index].env.data = (char *)malloc(len);
	bzero(fb[index].env.data, len);
	p = fb[index].env.data;
	for (i = 0; i < MAX_CACHE_ENTRY; i++) {
		int l;
		if (!fb[index].cache[i].name || !fb[index].cache[i].value)
			break;
		l = strlen(fb[index].cache[i].name) + strlen(fb[index].cache[i].value) + 2;
		if (p - fb[index].env.data + 2 >= fb[index].flash_max_len) {
			LIBNV_ERROR("ENV_BLK_SIZE 0x%x is not enough!", ENV_BLK_SIZE);
			FREE(fb[index].env.data);
			return -1;
		}
		snprintf(p, l, "%s=%s", fb[index].cache[i].name, fb[index].cache[i].value);
		p += l;
	}
	*p = '\0'; //ending null

	//calculate crc
	fb[index].env.crc = (unsigned long)crc32(0, (unsigned char *)fb[index].env.data, len);
	printf("Commit crc = %x\n", (unsigned int)fb[index].env.crc);

	//write crc to flash
	to = fb[index].flash_offset;
	len = sizeof(fb[index].env.crc);
	flash_write((char *)&fb[index].env.crc, to, len);

	//write data to flash
	to = to + len;
	len = fb[index].flash_max_len - len;
	flash_write(fb[index].env.data, to, len);
	FREE(fb[index].env.data);
#endif

	fb[index].dirty = 0;

	return 0;
}

/*
 * clear flash by writing all 1's value
 */
int nvram_clear(int index)
{
#ifdef CONFIG_KERNEL_NVRAM
	int fd;
	nvram_ioctl_t nvr;
#else
	unsigned long to;
	int len;
#endif

	LIBNV_PRINT("--> nvram_clear %d\n", index);
	LIBNV_CHECK_INDEX(-1);
	nvram_close_ralink(index);

#ifdef CONFIG_KERNEL_NVRAM
	nvr.index = index;
	fd = open(NV_DEV, O_RDONLY);
	if (fd < 0) {
		perror(NV_DEV);
		return -1;
	}
	if (ioctl(fd, RALINK_NVRAM_IOCTL_CLEAR, &nvr) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
#else
	//construct all 1s env block
	len = fb[index].flash_max_len - sizeof(fb[index].env.crc);
	fb[index].env.data = (char *)malloc(len);
	memset(fb[index].env.data, 0xFF, len);

	//calculate and write crc
	fb[index].env.crc = (unsigned long)crc32(0, (unsigned char *)fb[index].env.data, len);
	to = fb[index].flash_offset;
	len = sizeof(fb[index].env.crc);
	flash_write((char *)&fb[index].env.crc, to, len);

	//write all 1s data to flash
	to = to + len;
	len = fb[index].flash_max_len - len;
	flash_write(fb[index].env.data, to, len);
	FREE(fb[index].env.data);
	LIBNV_PRINT("clear flash from 0x%x for 0x%x bytes\n", (unsigned int *)to, len);
#endif

	fb[index].dirty = 0;
	return 0;
}

int getNvramNum(void)
{
	return FLASH_BLOCK_NUM;
}

unsigned int getNvramOffset(int index)
{
	LIBNV_CHECK_INDEX(0);
	return fb[index].flash_offset;
}

char *getNvramName(int index)
{
	LIBNV_CHECK_INDEX(NULL);
	return fb[index].name;
}

unsigned int getNvramBlockSize(int index)
{
	LIBNV_CHECK_INDEX(0);
	return fb[index].flash_max_len;
}

unsigned int getNvramIndex(char *name)
{
	int i;
	for (i = 0; i < FLASH_BLOCK_NUM; i++) {
		if (!strcmp(fb[i].name, name)) {
			return i;
		}
	}
	return -1;
}

void toggleNvramDebug()
{
	if (libnvram_debug) {
		libnvram_debug = 0;
		printf("%s: turn off debugging\n", __FILE__);
	}
	else {
		libnvram_debug = 1;
		printf("%s: turn ON debugging\n", __FILE__);
	}
}

