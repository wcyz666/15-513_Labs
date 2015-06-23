/*
 *	Andrew ID: chengw1@andrew.cmu.edu
 * 	Name: 	   Wang, Cheng
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//Include unistd.h for all system call 
//such as access()
#include <unistd.h>
#include <getopt.h>
#include "cachelab.h"


#define boolean int
#define INT_MAX ~(1 << 31)
#define false 0
#define true 1
#define MAX_LINE_LENGTH 80


typedef struct _CacheOptions {
	boolean isVerbose;
	int setCount;
	int lineCount;
	int blockSize;
	char* traceFileName;	
} CacheOptions;

typedef struct _SimCache {
	boolean isValid;
	int leastUsed;
	int tag;
} SimCache;

typedef struct _CacheResult {
	int hitCount;
	int missCount;
	int evictCount;
} CacheResult;

boolean checkSimCache(SimCache*, int, int, int*);
void freeCache(SimCache**, int);
unsigned int getSetIndex(long int, CacheOptions);
int getTag(long int, CacheOptions);
void loadData2Cache(SimCache*, int, int, int);
SimCache** initCache(CacheOptions);
CacheOptions parseArgs(int, char* []);
void printError(char*);
void printUsage();
CacheResult simCacheResult(CacheOptions);


/*
 * checkSimCache - This function checks the simulated cache with the given
 * 	   tag and return a boolean value suggesting whether this is a cache 
 *	   hit or miss.
 * 
 *	   @ lineNo. 
 *	       This parameter will be changed during function call.
 *		   If this is a cache hit, lineNo will store the number of the hit
 *		   line. If this is a cache miss, there will be two situations.
 *		       1. There exists available line for memory load. lineNo will 
 *			   	  be a positive number. It equals to 
 *				  [ available_line_no + 1 ].
 *			   2. These is no available line and a eviction should be done.
 *				  lineNo will be a negative number. It equals to
 *				  [ - (lease_used_line_no + 1) ].
 */
boolean checkSimCache(SimCache* set, int tag, int lineCount, int* lineNo) {
	
	int i, 
		leastUsed = INT_MAX,
		leastUsedLineNo = -1,
		emptyLine = -1;
	
	for (i = 0; i < lineCount; i++) {

		if (set[i].isValid) {

			if (set[i].leastUsed < leastUsed) {
				leastUsed = set[i].leastUsed;
				leastUsedLineNo = i;
			}
			//We have found a hit, 0 is returned.
			if (set[i].tag == tag) {
				*lineNo = i;
				return true;
			}
		}
		else if (emptyLine == -1) {
			emptyLine = i;
		}
	}

	//When we reach here, it means that we have found a miss.
	//If there is an empty line, we return pos + 1 in order 
	//to distinguish it from a hit.
	//Otherwise, we return the reverse number of the least 
	//used line + 1.
	if (emptyLine != -1) {
		*lineNo =  emptyLine + 1;
	}
	else {
		*lineNo =  -(leastUsedLineNo + 1);
	}
	return false;	
}


void freeCache(SimCache** simCache, int setCount) {
	
	int i;

	for (i = 0; i < setCount; i++) {
		free(simCache[i]);
	}	

	free(simCache);
}

/*  getSetIndex - This function receives address and compute the set index
 *  	Same for getTag
 */		
unsigned int getSetIndex(long int addr, CacheOptions caOpt) {
	long int mask = ~((-1l) << (caOpt.setCount + caOpt.blockSize));
	return (unsigned int)((addr & mask) >> caOpt.blockSize);
}


int getTag(long int addr, CacheOptions caOpt) {
	return (int)(addr >> (caOpt.blockSize + caOpt.setCount));
}


void loadData2Cache(SimCache* set, int tag, int leastUsed, int lineNo) {
	set[lineNo].tag = tag;
	set[lineNo].leastUsed = leastUsed;
	set[lineNo].isValid = true;
}


SimCache** initCache(CacheOptions caOpt) {

	int i, length;
	SimCache** simCache;
	//The size of the array is 2^(s);
	simCache = (SimCache **) malloc(sizeof(SimCache*) * (1 << caOpt.setCount));
	
	for (i = 0, length = (1 << caOpt.setCount); i < length; i++) {
		simCache[i] = (SimCache *) malloc (sizeof(SimCache) * caOpt.lineCount);
		memset(simCache[i], sizeof(simCache[i]), 0);
	}
	return simCache;
}


CacheOptions parseArgs(int argc, char* argv[]) {
	
	CacheOptions caOptions = {
		.isVerbose = false,
		.setCount = -1,
		.lineCount = -1,
		.blockSize = -1,
		.traceFileName = NULL,
	};

    char c;
	//Variable [count] is used to check if there is redundant input options.
	//such as ./csim -v -s 1 1 -b 1... (Two arguments for -s) 
	//Each time getopt returns an valid result, we assume two arguments are 
	//parsed. if not, we will reduce [count] by 1.
	//After all argeument are parsed, we simply check if [count] equals to 
	//[argc]
	int count = 1;
	
	
	while ((c = getopt(argc, argv, "hvs:E:b:t:")) != -1)
	{
		count += 2;
		switch (c)
		{
			case 'v':
				caOptions.isVerbose = true;
				count--;
				break;

			case 'h':
				printUsage();
				exit(0);

			case 's':
				caOptions.setCount = atoi(optarg);
				break;

			case 'E':
				caOptions.lineCount = atoi(optarg);
				break;

			case 'b':
				caOptions.blockSize = atoi(optarg);
				break;

			case 't':
				caOptions.traceFileName = optarg;
				break;

			// other options("?") and default are all regarded as invalid input
			case '?':
			default:
				printError("Invalid option.");
				printUsage();
				exit(1);
		}
	}

	if (count != argc || caOptions.lineCount <= 0 || caOptions.setCount <= 0 \
		|| caOptions.blockSize <= 0) {
		printError("Missing required command line argument");
		printUsage();
		exit(1);
	}

	if (access(caOptions.traceFileName, F_OK) == -1) {
		printError("Trace file not exist.");
		printUsage();
		exit(1);
	}

	return caOptions;
}


void printError(char* errMsg) {
	
	printf("./csim: %s\n", errMsg);
}


void printUsage() {

	printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
	printf("Options:\n");
	printf("  -h         Print this help message.\n");
	printf("  -v         Optional verbose flag.\n");
	printf("  -s <num>   Number of set index bits.\n");
	printf("  -E <num>   Number of lines per set.\n");
	printf("  -b <num>   Number of block offset bits.\n");
	printf("  -t <file>  Trace file.\n\n");
	printf("Examples:\n");
	printf("  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n");
	printf("  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}


CacheResult simCacheResult(CacheOptions caOpt) {

	CacheResult caResult = {
		.hitCount = 0,
		.missCount = 0,
		.evictCount = 0,
	};	
	FILE* fpInput;
	char lineBuf[MAX_LINE_LENGTH + 1];
	char* stopString = ",";
	char operation;
	long int address;
	int tag,
		operationId = 0,
		lineNo;
	unsigned int setIndex;
	SimCache** simCache = NULL;

	if ((fpInput = fopen(caOpt.traceFileName, "rb")) == NULL) {
		printError("Cannot open the trace file.");
		printUsage();
		exit(1);
	}

	simCache = initCache(caOpt);

	for ( ; ; ) {	
		fgets(lineBuf, MAX_LINE_LENGTH, fpInput);

		if (feof(fpInput))
			break;

		lineBuf[(int)strlen(lineBuf) - 1] = '\0';
		
		//ignore all instruction operations.
		if (lineBuf[0] == 'I') {
			continue;
		}
		
		//operationId is used as leastUsed
		++operationId; 

		operation = lineBuf[1];

		//[lineBuf + 2] means that the first two chars are skipped.
		//Since the address begins at the third character, and it 
		//is stopped with a comma.
		address = strtol(lineBuf + 2, &stopString, 16);
		setIndex = getSetIndex(address, caOpt);	
		tag = getTag(address, caOpt);
		
		if (checkSimCache(simCache[setIndex], tag, caOpt.lineCount, \
			&lineNo)) {
			simCache[setIndex][lineNo].leastUsed = operationId;	
			caResult.hitCount++;
			if (caOpt.isVerbose)
				strcat(lineBuf, " hit");
		}
		else {
			caResult.missCount++;
			if (caOpt.isVerbose)
				strcat(lineBuf, " miss");

			if (lineNo < 0) {
				lineNo = -lineNo;
				caResult.evictCount++;
				if (caOpt.isVerbose)
					strcat(lineBuf, " eviction");
			}
			
			loadData2Cache(simCache[setIndex], tag, operationId, lineNo - 1);
		}

		if (operation == 'M') {
			if (caOpt.isVerbose)
				strcat(lineBuf, " hit");
			caResult.hitCount++;
		}
		if (caOpt.isVerbose)
			printf("%s\n", lineBuf + 1);
	}

	freeCache(simCache, 1 << caOpt.setCount);
	fclose(fpInput);

	return caResult;
}


int main(int argc, char* argv[]) { 
	
	CacheOptions caOptions;
	CacheResult caResult;

	caOptions = parseArgs(argc, argv);
	caResult = 	simCacheResult(caOptions);

	printSummary(caResult.hitCount, caResult.missCount, caResult.evictCount);

	return 0;
}
