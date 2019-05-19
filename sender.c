#include "util.h"

/*
 * Execution state of the program, with the variables
 * that we need to pass around the various functions.
 */
struct state {
    char *buffer;
    struct Node *eviction_set;
    int interval;
    uint64_t cache_region;
    bool debug;
    bool benchmark_mode;
};

/*
 * Parses the arguments and flags of the program and initializes the struct state
 * with those parameters (or the default ones if no custom flags are given).
 */
void init_state(struct state *state, int argc, char **argv)
{
    // The following calculations are based on the paper:
    //      C5: Cross-Cores Cache Covert Channel (dimva 2015)
    int L3_way_stride = ipow(2, LOG_CACHE_SETS_L3 + LOG_CACHE_LINESIZE);
    int bsize = CACHE_WAYS_L3 * L3_way_stride;

    // Allocate a buffer of the size of the LLC
    state->buffer = malloc((size_t) bsize);
    // for (uint32_t i = 0; i < b; i += 64) {
    //     *(state->buffer + i) = 0x5;
    // }
    printf("buffer pointer addr %p\n", state->buffer);

    // Set some default state values.
    state->eviction_set = NULL;
    state->debug = false;
    state->benchmark_mode = false;

    // This number may need to be tuned up to the specific machine in use
    // NOTE: Make sure that interval is the same in both sender and receiver
    state->interval = 160;
    state->cache_region = 0x0;


    // Parse the command line flags
    //      -d is used to enable the debug prints
    //      -b is used to enable the benchmark mode (to measure the sending bitrate)
    //      -i is used to specify a custom value for the time interval
    int option;
    while ((option = getopt(argc, argv, "di:w:br:")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'b':
                state->benchmark_mode = true;
                break;
            case 'i':
                state->interval = atoi(optarg);
                break;
            case 'r':
                state->cache_region = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }

    // Construct the eviction_set by taking the addresses that have cache set index 0
    // There should be 128 such addresses in our buffer:
    //  one per line per cache set 0 of each slice (8 * 16).
    uint32_t eviction_set_size = 0;
    for (int set_index = 0; set_index < CACHE_SETS_L3; set_index++) {
        for (int line_index = 0; line_index < CACHE_WAYS_L3; line_index++) {

            ADDR_PTR addr = (ADDR_PTR) (state->buffer + \
                    set_index * CACHE_LINESIZE + line_index * L3_way_stride);
            if (get_cache_set_index(addr) == state->cache_region) {
                append_string_to_linked_list(&state->eviction_set, addr);
                eviction_set_size++;
            }
        }
    }
    printf("Found eviction_set size of %u\n", eviction_set_size);
}

/*
 * Sends a bit to the receiver by repeatedly flushing the addresses of the eviction_set
 * for the clock length of state->interval when we are sending a one, or by doing nothing
 * for the clock length of state->interval when we are sending a zero.
 */
void send_bit(bool one, const struct state *state)
{
    clock_t start_t, curr_t;

    start_t = clock();
    curr_t = start_t;

    if (one) {
        while ((curr_t - start_t) < state->interval) {
            struct Node *current = state->eviction_set;
            while (current != NULL && (curr_t - start_t) < state->interval) {
                ADDR_PTR addr = current->addr;
                clflush(addr);

                curr_t = clock();
                current = current->next;
            }
        }

    } else {
        start_t = clock();
        while (clock() - start_t < state->interval) {}
    }
}

int main(int argc, char **argv)
{
    // Initialize state and local variables
    struct state state;
    init_state(&state, argc, argv);
    clock_t start_t, end_t;
    int sending = 1;
    printPID();
    printf("Please type a message (exit to stop).\n");
    while (sending) {

        // Get a message to send from the user
        printf("< ");
        char text_buf[128];
        fgets(text_buf, sizeof(text_buf), stdin);

        if (strcmp(text_buf, "exit\n") == 0) {
            sending = 0;
        }

        // Convert that message to binary
        char *msg = string_to_binary(text_buf);
        if (state.debug) {
            printf("%s\n", msg);
        }

        // If we are in benchmark mode, start measuring the time
        if (state.benchmark_mode) {
            start_t = clock();
        }

        // Send a '10101011' byte to let the receiver detect that
        // I am about to send a start string and sync
        for (int i = 0; i < 6; i++) {
            send_bit(i % 2 == 0, &state);
        }
        send_bit(true, &state);
        send_bit(true, &state);

        // Send the message bit by bit
        size_t msg_len = strlen(msg);
        for (int ind = 0; ind < msg_len; ind++) {
            if (msg[ind] == '0') {
                send_bit(false, &state);
            } else {
                send_bit(true, &state);
            }
        }

        // If we are in benchmark mode, finish measuring the
        // time and print the bit rate.
        if (state.benchmark_mode) {
            end_t = clock();
            printf("Bitrate: %.2f Bytes/second\n",
                   ((double) strlen(text_buf)) / ((double) (end_t - start_t) / CLOCKS_PER_SEC));
        }
    }

    printf("Sender finished\n");
    return 0;
}
