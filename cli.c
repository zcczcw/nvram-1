#include "cli.h"
extern nvram_handle_t *nvram_h ;

int _do_show(nvram_handle_t *nvram)
{
	nvram_tuple_t *t;
	int stat = 1;

	if( (t = _nvram_getall(nvram)) != NULL )
	{
		while( t )
		{
			printf("%s=%s\n", t->name, t->value);
			t = t->next;
		}

		stat = 0;
	}

	return stat;
}

int do_show()
{
	if(nvram_h<0) {
		nvram_h = _nvram_open_rdonly();
		if(NULL == nvram_h) {
			_nvram_close(nvram_h);
		}
	}
	printf("__%s_%d:%08x\r\n", __FUNCTION__, __LINE__, nvram_h);

	return _do_show(nvram_h);
}

int _do_get(nvram_handle_t *nvram, const char *var)
{
	const char *val;
	int stat = 1;

	if( (val = _nvram_get(nvram, var)) != NULL )
	{
		printf("%s\n", val);
		stat = 0;
	}

	return stat;
}

int do_get(const char *var)
{
	if(nvram_h<0) {
		nvram_h = _nvram_open_rdonly();
		if(NULL == nvram_h) {
			_nvram_close(nvram_h);
		}
	}
	printf("__%s_%d:%08x\r\n", __FUNCTION__, __LINE__, nvram_h);
	return _do_get(nvram_h, var);
}

int _do_unset(nvram_handle_t *nvram, const char *var)
{
	return _nvram_unset(nvram, var);
}

int do_unset(const char *var)
{
	if(nvram_h<0) {
		nvram_h = _nvram_open_staging();
		if(NULL == nvram_h) {
			_nvram_close(nvram_h);
		}
	}

	printf("__%s_%d:%08x\r\n", __FUNCTION__, __LINE__, nvram_h);
	return _do_unset(nvram_h, var);
}

int _do_set(nvram_handle_t *nvram, const char *pair)
{
	char *val = strstr(pair, "=");
	char var[strlen(pair)];
	int stat = 1;

	if( val != NULL )
	{
		memset(var, 0, sizeof(var));
		strncpy(var, pair, (int)(val-pair));
		stat = _nvram_set(nvram, var, (char *)(val + 1));
	}

	return stat;
}

int do_set(const char *pair)
{
	if(nvram_h<0) {
		nvram_h = _nvram_open_staging();
		if(NULL == nvram_h) {
			_nvram_close(nvram_h);
		}
	}

	printf("__%s_%d:%08x\r\n", __FUNCTION__, __LINE__, nvram_h);
	return _do_set(nvram_h, pair);
}

int _do_info(nvram_handle_t *nvram)
{
	nvram_header_t *hdr = _nvram_header(nvram);

	/* CRC8 over the last 11 bytes of the header and data bytes */
	uint8_t crc = hndcrc8((unsigned char *) &hdr[0] + NVRAM_CRC_START_POSITION,
		hdr->len - NVRAM_CRC_START_POSITION, 0xff);

	/* Show info */
	printf("Magic:         0x%08X\n",   hdr->magic);
	printf("Length:        0x%08X\n",   hdr->len);
	printf("Offset:        0x%08X\n",   nvram->offset);

	printf("CRC8:          0x%02X (calculated: 0x%02X)\n",
		hdr->crc_ver_init & 0xFF, crc);

	printf("Version:       0x%02X\n",   (hdr->crc_ver_init >> 8) & 0xFF);
	printf("SDRAM init:    0x%04X\n",   (hdr->crc_ver_init >> 16) & 0xFFFF);
	printf("SDRAM config:  0x%04X\n",   hdr->config_refresh & 0xFFFF);
	printf("SDRAM refresh: 0x%04X\n",   (hdr->config_refresh >> 16) & 0xFFFF);
	printf("NCDL values:   0x%08X\n\n", hdr->config_ncdl);

	printf("%i bytes used / %i bytes available (%.2f%%)\n",
		hdr->len, NVRAM_SPACE - hdr->len,
		(100.00 / (double)NVRAM_SPACE) * (double)hdr->len);

	return 0;
}

int do_info()
{
	if(nvram_h<0) {
		nvram_h = _nvram_open_rdonly();
		if(NULL == nvram_h) {
			_nvram_close(nvram_h);
		}
	}
	printf("__%s_%d:%08x\r\n", __FUNCTION__, __LINE__, nvram_h);
	return _do_info(nvram_h);
}

//TODO
int do_export(const char *to_file)
{
	int stat = 1;
	nvram_export(to_file);
	return stat;
}

//TODO
int do_import(const char *to_file)
{
	int stat = 1;

	return stat;
}

#if 0
int main( int argc, const char *argv[] )
{
	nvram_handle_t *nvram;
	int commit = 0;
	int write = 0;
	int stat = 1;
	int done = 0;
	int i;

	/* Ugly... iterate over arguments to see whether we can expect a write */
	for( i = 1; i < argc; i++ )
		if( ( !strcmp(argv[i], "set")   && ++i < argc ) ||
			( !strcmp(argv[i], "unset") && ++i < argc ) ||
			!strcmp(argv[i], "commit") )
		{
			write = 1;
			break;
		}


	nvram = write ? _nvram_open_staging() : _nvram_open_rdonly();

	if( nvram != NULL && argc > 1 )
	{
		for( i = 1; i < argc; i++ )
		{
			if( !strcmp(argv[i], "show") )
			{
				stat = _do_show(nvram);
				done++;
			}
			else if( !strcmp(argv[i], "info") )
			{
				stat = _do_info(nvram);
				done++;
			}
			else if( !strcmp(argv[i], "get") 
				|| !strcmp(argv[i], "unset") 
				|| !strcmp(argv[i], "set") 
				|| !strcmp(argv[i], "export") 
				|| !strcmp(argv[i], "import") 
				)
			{
				if( (i+1) < argc )
				{
					switch(argv[i++][0])
					{
						case 'g':
							stat = _do_get(nvram, argv[i]);
							break;

						case 'u':
							stat = _do_unset(nvram, argv[i]);
							break;

						case 's':
							stat = _do_set(nvram, argv[i]);
							break;

						case 'e':
							stat = do_export(argv[i]);
							break;

						case 'i':
							stat = do_import(argv[i]);
							break;
					}
					done++;
				}
				else
				{
					fprintf(stderr, "Command '%s' requires an argument!\n", argv[i]);
					done = 0;
					break;
				}
			}
			else if( !strcmp(argv[i], "commit") )
			{
				commit = 1;
				done++;
			}
			else
			{
				fprintf(stderr, "Unknown option '%s' !\n", argv[i]);
				done = 0;
				break;
			}
		}

		if( write )
			stat = _nvram_commit(nvram);

		_nvram_close(nvram);

		if( commit )
			stat = staging_to_nvram();
	}

	if( !nvram )
	{
		fprintf(stderr,
			"Could not open nvram! Possible reasons are:\n"
			"	- No device found (/proc not mounted or no nvram present)\n"
			"	- Insufficient permissions to open mtd device\n"
			"	- Insufficient memory to complete operation\n"
			"	- Memory mapping failed or not supported\n"
		);

		stat = 1;
	}
	else if( !done )
	{
		fprintf(stderr,
			"Usage:\n"
			"	nvram show\n"
			"	nvram info\n"
			"	nvram get variable\n"
			"	nvram set variable=value [set ...]\n"
			"	nvram unset variable [unset ...]\n"
			"	nvram commit\n"
			"	nvram export/import backup_file\n"
		);

		stat = 1;
	}

	return stat;
}
#else
int main( int argc, const char *argv[] )
{
	int commit = 0;
	int write = 0;
	int stat = 1;
	int done = 0;
	int i;

	/* Ugly... iterate over arguments to see whether we can expect a write */
	for( i = 1; i < argc; i++ ) {
		if( ( !strcmp(argv[i], "set")   && ++i < argc ) ||
			( !strcmp(argv[i], "unset") && ++i < argc ) ||
			!strcmp(argv[i], "commit") )
		{
			write = 1;
			break;
		}
	}



	if( argc > 1 )
	{
		for( i = 1; i < argc; i++ )
		{
			if( !strcmp(argv[i], "show") )
			{
				stat = do_show();
				done++;
			}
			else if( !strcmp(argv[i], "info") )
			{
				stat = do_info();
				done++;
			}
			else if( !strcmp(argv[i], "get") 
				|| !strcmp(argv[i], "unset") 
				|| !strcmp(argv[i], "set") 
				|| !strcmp(argv[i], "export") 
				|| !strcmp(argv[i], "import") 
				)
			{
				if( (i+1) < argc )
				{
					switch(argv[i++][0])
					{
						case 'g':
							stat = do_get(argv[i]);
							break;

						case 'u':
							stat = do_unset(argv[i]);
							break;

						case 's':
							stat = do_set(argv[i]);
							break;

						case 'e':
							stat = do_export(argv[i]);
							break;

						case 'i':
							stat = do_import(argv[i]);
							break;
					}
					done++;
				}
				else
				{
					fprintf(stderr, "Command '%s' requires an argument!\n", argv[i]);
					done = 0;
					break;
				}
			}
			else if( !strcmp(argv[i], "commit") )
			{
				commit = 1;
				done++;
			}
			else
			{
				fprintf(stderr, "Unknown option '%s' !\n", argv[i]);
				done = 0;
				break;
			}
		}

		if( write )
			stat = nvram_commit();

		if( commit )
			stat = staging_to_nvram();
	}

	else if( !done )
	{
		fprintf(stderr,
			"Usage:\n"
			"	nvram show\n"
			"	nvram info\n"
			"	nvram get variable\n"
			"	nvram set variable=value [set ...]\n"
			"	nvram unset variable [unset ...]\n"
			"	nvram commit\n"
			"	nvram export/import backup_file\n"
		);

		stat = 1;
	}

	return stat;
}
#endif
