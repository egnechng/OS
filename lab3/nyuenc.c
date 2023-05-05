#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#define CHUNK_SIZE 4096
#define MAX_FILES 100

/*
    References:
    -Man Pages: getopt(3), mmap(), fstat(), pthread_create(3) and other thread-related functions
    -fwrite() examples: https://www.geeksforgeeks.org/fwrite-vs-write/
    -Discord Chat
    -Very helpful video on Thread Pools: https://www.youtube.com/watch?v=_n2hE2gyPxU
*/

//synchronization
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t chunk_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t write_cond = PTHREAD_COND_INITIALIZER;

typedef struct{
    int size;
    char *result; //compressed data
    unsigned char* counts;
} Chunk;

Chunk chunks[1000000];
int num_chunks = 0;
int available[1000000];

//int last_encoded = -1;

typedef struct{
    char *data; //data start
    int size; //in bytes
    int id;
} Task;
int num_tasks = 0; //"value"
int total_tasks = 0;

typedef struct node{
    Task task;
    struct node* next;
} Node;

typedef struct{
    Node* head;
    Node* tail;
} TaskQueue;

void init_queue(TaskQueue* queue){
    queue->head = NULL;
    queue->tail = NULL;
}

void push(TaskQueue* queue, Task task){
    pthread_mutex_lock(&queue_mutex);
    
    Node* new_node = (Node*) malloc(sizeof(Node));
    new_node->task = task;
    new_node->next = NULL;

    if (queue->tail == NULL){
        queue->head = new_node;
        queue->tail = new_node;
    }else{
        queue->tail->next = new_node;
        queue->tail = new_node;
    }
    num_tasks++;
    total_tasks++;

    pthread_cond_signal(&cond); //signal thread
    pthread_mutex_unlock(&queue_mutex);
}

Task pop(TaskQueue* queue){
    if (queue->head == NULL){
        perror("error: queue empty\n");
        exit(EXIT_FAILURE);
    }

    Node* removed_node = queue->head;
    Task removed_task = removed_node->task;
    queue->head = removed_node->next;
    free(removed_node);

    if (queue->head == NULL){
        queue->tail = NULL;
    }
    
    //num_tasks--;
    return removed_task;
}
TaskQueue queue;

void encode_task(Task *task){

    char *data = task->data;
    int size = task->size;
    char *result = malloc(sizeof(char) * (size *2 + 1));//leave more than enough space for compressed result
    unsigned char *counts = malloc(sizeof(char) * (size *2 + 1));
    int result_size = 0;
    
    int i = 0, j = 0;
    while (i < size){
        unsigned char count = 1;
        char current = data[i];
        while (i < size - 1 && data[i] == data[i+1] && count < 255){
            count++;
            i++;
        }
        result[j] = current;
        counts[j] = count;
        j++;
        result_size += 2;
        i++;
    }

    Chunk chunk = {
        .size = result_size,
        .result = result,
        .counts = counts
    };

    pthread_mutex_lock(&chunk_mutex);
    int index = task->id;
    chunks[index] = chunk;
    available[index] = 1;
    num_chunks++;
    //total_chunks++;
    pthread_cond_signal(&write_cond);
    pthread_mutex_unlock(&chunk_mutex);
    
    //if main thread is stopped on a hole, we have to signal main thread to continue
    /*
    if (index == last_encoded + 1){
        //pthread_cond_signal(&write_cond);
    } //signal main thread to write
    */
    
}

void* worker_function(){
    
    while(1){
      
        Task task;
        //CRITICAL SECTION:
        pthread_mutex_lock(&queue_mutex);
        while(num_tasks == 0){
            pthread_cond_wait(&cond, &queue_mutex);
        }
        //"pop" task from the task queue
        task = pop(&queue);
        num_tasks--;
        pthread_mutex_unlock(&queue_mutex);

        //RLE encode the task
        encode_task(&task);
    }

}

int main(int argc, char* argv[]){
    
    int num_jobs = 0;
    int opt;
    char* file_names[MAX_FILES];
    int num_files = 0;
    int invalid = 0;
    init_queue(&queue);

    //handle options "-j jobs"
    while ((opt = getopt(argc, argv, "j:")) != 1){
        switch (opt){
            case 'j':
                num_jobs = atoi(optarg);
                //printf("%d\n",num_jobs);
                break;
            default:
                invalid = 1;
                break;
        }
        if (invalid){
            break;
        }
    } 

    while (optind < argc && num_files < MAX_FILES){
        file_names[num_files++]= argv[optind++];
    }

    if (num_files == 0){
        fprintf(stderr, "No file provided\n");
        exit(EXIT_FAILURE);
    }

    //TEST: print file names and # threads
    /*
    printf("Number of files: %d\n", num_files);
    printf("Number of worker threads: %d\n", num_jobs);
    for (int i = 0; i < num_files; i++) {
        printf("File name: %s\n", file_names[i]);
    }*/

    //INITIALIZE threads, mutex and condition variable
    pthread_t threads[num_jobs]; //Thread Pool
    for (int i = 0; i < num_jobs; i++){
        if (pthread_create(&threads[i], NULL, worker_function, NULL) != 0){
            fprintf(stderr,"Thread fail.\n");
        }
    }
   
    int id_counter = 0;
    //char *data;
    //OPEN FILES AND MAP TO MEMORY
    for (int i = 0; i < num_files; i++){

        int fd = open(file_names[i], O_RDONLY);
        if (fd == -1){
            fprintf(stderr, "Error opening file %s\n", file_names[i]);
           
        }

        struct stat sb;
        if (fstat(fd, &sb) == -1){
            fprintf(stderr, "File size error\n");
            exit(EXIT_FAILURE);
        }
        
        
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED){
            continue;
            /*
            fprintf(stderr, "Mapping error\n");
            exit(EXIT_FAILURE);
            */
        }
        
        //create TASKS, split into 4KB chunks
        off_t offset = 0;
        while (offset < sb.st_size){
            size_t remaining = sb.st_size - offset;
            
            size_t size = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;

            char *start = addr + offset;
            
            int *id = malloc(sizeof(int));
            *id = id_counter;
            
            Task task ={
                .id = *id,
                .size = size,
                .data = start
            };
            
            offset += size;

            available[id_counter] = 0;
            id_counter++;
            push(&queue, task);
            //add_task(task); 
        }
        close(fd);
        
    }
    
    //SEQUENTIAL PART (if no thread)
    
    if (num_jobs == 0){
        char last_letter = 0;
        unsigned char last_sum = 0; 
        int begin_chunk = 1;  
        for (int j = 0; j < num_tasks; j++){
            Task t = pop(&queue);
            encode_task(&t);
        }
        int k = 0;

        while (k < total_tasks){
            Chunk chunk;
            chunk = chunks[k];

            char* result = chunk.result;
            unsigned char* counts = chunk.counts;
            int result_size = chunk.size/2;
            char current_char;
            unsigned char current_sum; 
            for (int i = 0; i < result_size; i++){
                    current_char = result[i];
                    current_sum = counts[i];

                if (last_letter == current_char && begin_chunk != 1){
                    last_sum = last_sum + current_sum;
                }else{
                    if (begin_chunk != 1){
                        fwrite(&last_letter, 1, 1, stdout);
                        fwrite(&last_sum, 1, 1 ,stdout);
                    }
                    //update last letter and sum
                    last_letter = current_char;
                    last_sum = current_sum;
                }
                begin_chunk = 0;
            }
            k++;
        }
        fwrite(&last_letter, 1, 1, stdout);
        fwrite(&last_sum, 1, 1, stdout); 

        exit(EXIT_SUCCESS);            
    }
    
    //If parallel--Stitch and Write RESULTS parallel
    int k = 0;
    char last_letter = 0;
    unsigned char last_sum = 0; 
    int begin_chunk = 1;  //keeping track of beginning of the chunk entry 
    while(k < total_tasks){
        Chunk chunk;

        pthread_mutex_lock(&chunk_mutex);
        while (available[k] == 0){
             pthread_cond_wait(&write_cond, &chunk_mutex);
        }
        chunk = chunks[k];
        
        char* result = chunk.result;
        unsigned char* counts = chunk.counts;
        int result_size = chunk.size/2;
       
        //Write
        //last_letter = result[0];
        //last_sum = counts[0];
        char current_char;
        unsigned char current_sum;
        
        for (int i = 0; i < result_size; i++){
            current_char = result[i];
            current_sum = counts[i];

            if (last_letter == current_char && begin_chunk != 1){
                last_sum = last_sum + current_sum;
            }else{
                if (begin_chunk !=1){
                    fwrite(&last_letter, 1, 1, stdout);
                    fwrite(&last_sum, 1, 1 ,stdout);
                }
                //update last letter and sum
                last_letter = current_char;
                last_sum = current_sum;
            }
            begin_chunk = 0;
        }
        k++;
        pthread_mutex_unlock(&chunk_mutex);
    }
    fwrite(&last_letter, 1, 1, stdout);
    fwrite(&last_sum, 1, 1, stdout);
    //exit(EXIT_SUCCESS);
    return 0;
}