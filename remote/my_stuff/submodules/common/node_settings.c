#include "node_settings.h"
#include "ztimer.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char myName[NAME_SIZE+1];
static uint16_t myId = 0xffff;
static uint16_t knownHosts[MAX_HOSTS];
static uint16_t numKnownHosts = 0;
static bool initialized = false;

// TODO get throttling here

int Cmd_GetTime(int argc, char **argv)
{
	uint32_t t = ztimer_now(ZTIMER_USEC);
	printf("%d\n", t);
	return 0;
}

int Cmd_SetName(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("argc %d needs to be 2\n");
		return 1;
	}
	strncpy((char *) &myName, argv[1], NAME_SIZE);
	myId = atoi((const char*) &myName);
	printf("Set name to %s (%d)\n", myName, myId);
	return 0;
}

int Cmd_GetName(int argc, char **argv)
{
	printf("%s\n", myName);
	return 0;
}

int Cmd_SetHosts(int argc, char **argv)
{
	if (argc == 1)
	{
		printf("Need hostnames!\n");
		return 1;
	}
	numKnownHosts = 0;
	for (int i = 1; i < argc; i++)
	{
		printf("New host m3-%s\n", argv[i]);
		knownHosts[i-1] = atoi(argv[i]);
		numKnownHosts++;
	}
	return 0;
}

int Cmd_GetHosts(int argc, char **argv)
{
	for (int i = 0; i < numKnownHosts; i++)
	{
		printf("Host %d = m3-%d\n", i, knownHosts[i]);
	}
	return 0;
}

int Cmd_StartProgram(int argc, char **argv)
{
	if (myId == 0xffff || numKnownHosts == 0)
	{
		printf("myId %x numKnownHosts %d BAD!\n", myId, numKnownHosts);
		return 1;
	}
	initialized = true;
	return 0;
}
