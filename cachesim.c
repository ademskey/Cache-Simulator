#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

/*
Global variables
    ADDRESS_LENGTH: Length of memory address in bits
*/
#define ADDRESS_LENGTH 64  // 64-bit memory addressing

/*
Structs:
    - line: Defines a cache line
    - set: Defines a cache set as an array of cache lines
    - cache: Defines a cache as an array of cache sets and settings
*/
typedef struct {
    int valid;  // Valid bit
    unsigned long tag;  // Tag bits
    int lru;  // Least recently Used (LRU) bit
} line;
 
typedef struct {
    line* lines;  // Array of cache lines
} set;

typedef struct {
    set* sets;  // Array of cache sets
    int s;  // Number of set index bits
    int E;  // Associativity (number of lines per set)
    int b;  // Number of block bits
    int S;  // Number of sets
} cache;

/*
Functions:
    - main: Gets command line argument and runs simulation
    - print_summary: Prints the summary of the cache simulation
    - print_usage: Prints the usage of the program
    - makecache: Initializes cache structure
    - freecache: Frees memory allocated for cache
    - runsim: Reads from file and performs operations to run simulation
    - access_cache: Accesses the cache and checks for hit or miss
    - lru_update: Updates cache based on LRU policy
    - find_lru: Finds the least recently used line in a set
*/
/////////////////////// Function prototypes ///////////////////////////
void print_summary(int hits, int misses, int evictions);
void print_usage(char* argv[]);
cache* makecache(int s, int E, int b);
void runsim(cache* c, FILE* tracefile, int* hits, int* misses, int* evictions, int* verbose);
void access_cache(cache* c, long unsigned int* address, int* hit, int* miss, int* evictions, int* verbose);
void lru_update(cache* c, set* inset, line* line);
int find_lru(cache* c, set* inset);
void freecache(cache* c);
int main(int argc, char* argv[])
{
	// Define variables
    int input; // getopt returns int for some reason
    int h = 0; // help flag that prints usage info
    int v = 0; // verbose flag that prints trace info
    int s = 0; // number of set index bits (S = 2^s is the number of sets)
    int E = 0; // Associativity (number of lines per set)
    int b = 0; // number of block bits (B = 2^b is the block size)
    char* t = NULL; // name of the valgrind trace to replay

    // Parse command line arguments
    while ((input = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (input) {
            case 'h':
                h = 1;  // Set help flag
                break;
            case 'v':
                v = 1;  // Set verbose flag
                break;
            case 's':
                s = atoi(optarg);  // Set number of set index bits
                break;
            case 'E':
                E = atoi(optarg);  // Set associativity (number of lines per set)
                break;
            case 'b':
                b = atoi(optarg);  // Set number of block bits
                break;
            case 't':
                t = optarg;  // Set name of valgrind trace to replay
                break;
            default:
                print_usage(argv);  // Print usage info and exit on invalid option
                exit(1); // Exit with code 1
        }
    }
    
    // Check for help or invalid parameters
    if (h == 1 || argv[1] == NULL || s == 0 || E == 0 || b == 0 || t == NULL) {
        print_usage(argv);
        return 0;
    }

    // Initialize variables for cache simulation
    int hit_count = 0, miss_count = 0, eviction_count = 0;
    cache* cachsim = makecache(s, E, b);
    
    printf("Initializing Cache Simulation\n");

    // Check for cache creation failure
    if (!cachsim) {
        printf("Error creating cache\n");
        return 1;
    }
    printf("Cache created\n");

    // Open trace file
    FILE* tracefile = fopen(t, "r");
    if (!tracefile) {
        printf("Error opening trace file. Make sure path and name is correct\n");
        return 1;
    }

    // Run the cache simulation
    printf("Running Cache Simulation\n");
    runsim(cachsim, tracefile, &hit_count, &miss_count, &eviction_count, &v);

    // Print summary
    printf("Results:\n");
    print_summary(hit_count, miss_count, eviction_count);

    // Cean up
    fclose(tracefile);
    freecache(cachsim);

    return 0;
}

void print_summary(int hits, int misses, int evictions){
    printf("hits:%d misses:%d evictions:%d\n", hits, misses, evictions);
}

void print_usage(char* argv[]){
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\nExamples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/trace01.dat\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/trace01.dat\n", argv[0]);
    exit(0);
}

cache* makecache(int s, int E, int b) {
    // Calculate the number of sets (S = 2^s)
    int S = 1 << s;

    // Allocate memory for the cache structure
    cache* cachesim = (cache*)malloc(sizeof(cache));
    if (!cachesim){
       printf("Error allocating memory for cache sim");
       return NULL; 
    } 

    // Initialize cache parameters
    cachesim->s = s;
    cachesim->E = E;
    cachesim->b = b;
    cachesim->S = S;

    // Allocate memory for the sets
    // had to add size_t cast to avoid warning (implicit conversion)
    cachesim->sets = (set*)malloc((size_t)S * sizeof(set));
    if (!cachesim->sets) { // Check for memory allocation of sets
        printf("Error allocating memory for cache sets");
        free(cachesim);
        return NULL;
    }

    // Allocate memory for the lines within each set
    for (int i = 0; i < S; i++) {
        cachesim->sets[i].lines = (line*)malloc((size_t)E * sizeof(line));
        if (!cachesim->sets[i].lines) { // Check for memory allocation of lines
            printf("Error allocating memory for cache lines");
            // Free memory if faliure detected
            for (int j = 0; j < i; j++) {
                free(cachesim->sets[j].lines);
            }
            free(cachesim->sets);
            free(cachesim);
            return NULL;
        }

        // Initialize each line's valid bit to 0 and other fields
        for (int j = 0; j < E; j++) {
            cachesim->sets[i].lines[j].valid = 0;
            cachesim->sets[i].lines[j].tag = 0;
            cachesim->sets[i].lines[j].lru = 0;
        }
    }
    return cachesim;  // Return the created cache
}

void freecache(cache* c) {
    if (c) {
        // Free each set's lines
        for (int i = 0; i < c->S; i++) {
            free(c->sets[i].lines);
        }

        // Free the sets array and cache structure
        free(c->sets);
        free(c);
    }
}

void runsim(cache* c, FILE* tracefile, int* hits, int* misses, int* evictions, int* verbose) {
    // Initialize variables
    char operation;
    long unsigned int address;
    int size;

    // Read the trace file
    while (fscanf(tracefile, " %c %lx,%d", &operation, &address, &size) > 0){
        // Check for a memory access
        if (operation == 'L' || operation == 'S'){
            // Load and store operations
            if(*verbose == 1){
                printf("%c %lx, %d ", operation, address, size);
                access_cache(c, &address, hits, misses, evictions, verbose);
                printf("\n");
            }
            else{
                access_cache(c, &address, hits, misses, evictions, verbose);
            }
        }
        if (operation == 'M'){
            // Modify operation is load and store combined
            if(*verbose == 1){
                printf("%c %lx, %d ", operation, address, size);
                access_cache(c, &address, hits, misses, evictions, verbose);
                access_cache(c, &address, hits, misses, evictions, verbose);
                printf("\n");
            }
            else{
                access_cache(c, &address, hits, misses, evictions, verbose);
                access_cache(c, &address, hits, misses, evictions, verbose);
            }
        }
        else{
            // Skip instruction load operations
            continue;
        }
    }
}

void access_cache(cache* c, long unsigned int* address, int* hit, int* miss, int* evictions, int* verb){
    // Calculate the set index and tag
    // Calculate set index by shifting the address right by b bits and masking with S - 1
        // Shift right by b bits to get rid of block bits
        // Mask with S - 1 to zero out all bits except the set index bits
    long unsigned int cache_set = (*address >> c->b) & ((1UL << c->s) - 1);

    // Calculate tag by shifting the address right by set idx and block bits
    long unsigned int tag = *address >> (c->s + c->b);

    // Check for a hit
    for (int i = 0; i < c->E; i++){
        if (c->sets[cache_set].lines[i].valid && c->sets[cache_set].lines[i].tag == tag){
            if (*verb == 1){
                printf("hit ");
            }
            *hit += 1;
            lru_update(c, &c->sets[cache_set], &c->sets[cache_set].lines[i]);
            return;
        }
    }
    // Check for a miss and find empty line
    for(int i = 0; i < c->E; i++){
        if (!c->sets[cache_set].lines[i].valid){ //Check for empty line
            c->sets[cache_set].lines[i].valid = 1;  // Set valid
            c->sets[cache_set].lines[i].tag = tag;
            if (*verb == 1){
                printf("miss ");
            }
            *miss += 1;
            lru_update(c, &c->sets[cache_set], &c->sets[cache_set].lines[i]);
            return;
        }
    }

    // Check for eviction
    int lru_idx = find_lru(c, &c->sets[cache_set]);
    if(*verb == 1){
        printf("miss eviction ");
    }
    *miss += 1;
    *evictions += 1;

    // Evict the LRU line
    c->sets[cache_set].lines[lru_idx].tag = tag; //update the tag
    lru_update(c, &c->sets[cache_set], &c->sets[cache_set].lines[lru_idx]); //run update
    return;
}

int find_lru(cache* c, set* inset){
    int lru_idx = 0;
    // Find the least recently used line
    for (int i = 0; i < c->E; i++){
        if (inset->lines[i].lru == c->E - 1){
            lru_idx = i;
            break;
        }
    }
    return lru_idx;
}

void lru_update(cache* c, set* inset, line* cache_line) {
    // Update the age of each line in the set
    for (int i = 0; i < c->E; i++){
        if (inset->lines[i].valid){
            if(inset->lines[i].lru < cache_line->lru){
                inset->lines[i].lru = inset->lines[i].lru + 1;
            }
        }
    }
    cache_line->lru = 0;
}