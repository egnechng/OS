#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define SHA_DIGEST_LENGTH 20

/** References:
 * https://www.tutorialspoint.com/c_standard_library/c_function_sprintf.htm
 * Lecture slides
 * Discord chat
*/

#pragma pack(push,1)
typedef struct BootEntry {
  unsigned char  BS_jmpBoot[3];     // Assembly instruction to jump to boot code
  unsigned char  BS_OEMName[8];     // OEM Name in ASCII
  unsigned short BPB_BytsPerSec;    // Bytes per sector. Allowed values include 512, 1024, 2048, and 4096
  unsigned char  BPB_SecPerClus;    // Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB or smaller
  unsigned short BPB_RsvdSecCnt;    // Size in sectors of the reserved area
  unsigned char  BPB_NumFATs;       // Number of FATs
  unsigned short BPB_RootEntCnt;    // Maximum number of files in the root directory for FAT12 and FAT16. This is 0 for FAT32
  unsigned short BPB_TotSec16;      // 16-bit value of number of sectors in file system
  unsigned char  BPB_Media;         // Media type
  unsigned short BPB_FATSz16;       // 16-bit size in sectors of each FAT for FAT12 and FAT16. For FAT32, this field is 0
  unsigned short BPB_SecPerTrk;     // Sectors per track of storage device
  unsigned short BPB_NumHeads;      // Number of heads in storage device
  unsigned int   BPB_HiddSec;       // Number of sectors before the start of partition
  unsigned int   BPB_TotSec32;      // 32-bit value of number of sectors in file system. Either this value or the 16-bit value above must be 0
  unsigned int   BPB_FATSz32;       // 32-bit size in sectors of one FAT
  unsigned short BPB_ExtFlags;      // A flag for FAT
  unsigned short BPB_FSVer;         // The major and minor version number
  unsigned int   BPB_RootClus;      // Cluster where the root directory can be found
  unsigned short BPB_FSInfo;        // Sector where FSINFO structure can be found
  unsigned short BPB_BkBootSec;     // Sector where backup copy of boot sector is located
  unsigned char  BPB_Reserved[12];  // Reserved
  unsigned char  BS_DrvNum;         // BIOS INT13h drive number
  unsigned char  BS_Reserved1;      // Not used
  unsigned char  BS_BootSig;        // Extended boot signature to identify if the next three values are valid
  unsigned int   BS_VolID;          // Volume serial number
  unsigned char  BS_VolLab[11];     // Volume label in ASCII. User defines when creating the file system
  unsigned char  BS_FilSysType[8];  // File system type label in ASCII
} BootEntry;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct DirEntry {
  unsigned char  DIR_Name[11];      // File name
  unsigned char  DIR_Attr;          // File attributes
  unsigned char  DIR_NTRes;         // Reserved
  unsigned char  DIR_CrtTimeTenth;  // Created time (tenths of second)
  unsigned short DIR_CrtTime;       // Created time (hours, minutes, seconds)
  unsigned short DIR_CrtDate;       // Created day
  unsigned short DIR_LstAccDate;    // Accessed day
  unsigned short DIR_FstClusHI;     // High 2 bytes of the first cluster address
  unsigned short DIR_WrtTime;       // Written time (hours, minutes, seconds
  unsigned short DIR_WrtDate;       // Written day
  unsigned short DIR_FstClusLO;     // Low 2 bytes of the first cluster address
  unsigned int   DIR_FileSize;      // File size in bytes. (0 for directories)
} DirEntry;
#pragma pack(pop)

void print_filename(unsigned char *DIR_Name){
  //printf("%s\n", DIR_Name);

  for (int i = 0; i < 8 && DIR_Name[i] != ' '; i++){
    printf("%c", DIR_Name[i]);
  }

  if (DIR_Name[8] != ' '){
    printf(".");
    for (int j = 8; j < 11; j++){
      if (DIR_Name[j] != ' ') printf("%c", DIR_Name[j]);
    }
  }
  
  printf(" ");
}

void print_dirname(unsigned char *DIR_Name){
  for (int i = 0; i < 8 && DIR_Name[i] != ' '; i++){
    printf("%c", DIR_Name[i]);
  }
  printf("/ ");
}


int main(int argc, char* argv[]){

    //MILESTONE 1 - VALIDATE USAGE
    int opt;
    char *diskname = NULL;
    char *filename = NULL;
    char *sha1 = NULL;

    int i_flag = 0, l_flag = 0, r_flag = 0, R_flag = 0, s_flag = 0;

    if (argc < 3){
      goto usage;
    }

    while ((opt = getopt(argc, argv, "ilr:R:s:")) != -1){
        switch(opt){
            case 'i': 
              if (l_flag || r_flag || R_flag){
                goto usage;
              }
              i_flag = 1;
              break; 
            case 'l': 
              if (i_flag || r_flag || R_flag){
                goto usage;
              }
              l_flag = 1;
              break;
            case 'r': //need argument, stored in optarg
              if (i_flag || l_flag || R_flag){
                goto usage;
              }
              r_flag = 1;
              filename = optarg;
              break;
            case 'R': 
              if (i_flag || l_flag || r_flag){
                goto usage;
              }
              R_flag = 1;
              filename = optarg;
              break;
            case 's':
              s_flag = 1;
              sha1 = optarg;
              break;
            default:
              goto usage;
        }
    }

    //check for diskname
    if (optind >= argc){
      goto usage;
    }
    diskname = argv[optind];

    //Make sure -R is followed by a SHA1 always
    if (R_flag && !s_flag){
      goto usage;
    }

    //OPEN DISK AND MAP TO MEMORY
    int fd = open(diskname, O_RDWR);
    if (fd == -1){
      perror("File open error\n");
    }
    struct stat sb;
    if (fstat(fd, &sb) == -1){
      perror("fstat error\n");
    }
    char *addr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED){
        perror("Mapping error\n");
        exit(EXIT_FAILURE);
    }
    
    BootEntry *boot_sector = (BootEntry *) addr; //cast beginning of disk memory to bootsector

    //MILESTONE 2 - PRINT FILE SYSTEM INFO
    if (i_flag){
        printf("Number of FATs = %d\n", boot_sector->BPB_NumFATs);
        printf("Number of bytes per sector = %d\n", boot_sector->BPB_BytsPerSec);
        printf("Number of sectors per cluster = %d\n", boot_sector->BPB_SecPerClus);
        printf("Number of reserved sectors = %d\n", boot_sector->BPB_RsvdSecCnt); 
        exit(EXIT_SUCCESS);
    }

    //MILESTONE 3 - LIST THE ROOT DIRECTORY
    //EOF >= 0x 0fff fff8
    unsigned int root_cluster = boot_sector->BPB_RootClus - 2;
    unsigned short bytes_per_sector = boot_sector->BPB_BytsPerSec;       
       
    //directory entry is addr + (reserved sector + #sectors_per_FAT * #FAT)*size_of_sector + (root_cluster - 2)*(sec_per_cluster * bytes per cluster)   
       
    char *dir_entry_addr = ( addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
        + (root_cluster*boot_sector->BPB_SecPerClus))*bytes_per_sector ); //points to beginning of root directory    
        
    //dir_entry_addr += 32*1;
    //DirEntry *dir_entry = (DirEntry *) dir_entry_addr;
    char* fat_addr = addr + (boot_sector->BPB_RsvdSecCnt * bytes_per_sector); // FAT comes after reserved sector
    int *fat = (int *) fat_addr;

    if (l_flag){
          
        int total_entries = 0;
        int bytes_searched = 0;
        int bytes_per_cluster = boot_sector->BPB_SecPerClus * bytes_per_sector;
        
        int curr_index = boot_sector->BPB_RootClus;

        while (1){
          
          DirEntry *d = (DirEntry *) dir_entry_addr;
          
          if (d->DIR_Name[0] == 0x00){
            break;
          }else if (d->DIR_Name[0] == 0xE5){
            //do nothing
          }else{  
            //Calculate starting cluster
            unsigned short lo = d->DIR_FstClusLO; 
            unsigned short hi = d->DIR_FstClusHI;
            
            unsigned int cluster_num = (hi << 8) | lo;

            unsigned int file_size = d->DIR_FileSize; 
            //if Directory
            if (d->DIR_Attr == 0x10){
              print_dirname(d->DIR_Name);
              printf("(starting cluster = %i)\n", cluster_num);
            }else if (file_size == 0){ //if empty
              print_filename(d->DIR_Name);
              printf("(size = %i)\n", file_size);
            }else{ //if regular file
              print_filename(d->DIR_Name);
              printf("(size = %i, ", file_size);
              printf("starting cluster = %i)\n", cluster_num);
            }         
          total_entries++;
          }
          dir_entry_addr += 32;
          bytes_searched += 32;

          if (bytes_searched == bytes_per_cluster){ //move to next cluster
              //update pointer to start of next cluster
              int next_cluster = fat[curr_index];
              if (next_cluster >= 0xffffff8){
                break;
              }
              else{
                curr_index = next_cluster;
                dir_entry_addr = addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
        + ( (curr_index-2) *boot_sector->BPB_SecPerClus))*bytes_per_sector ;
              }
              bytes_searched = 0; //reset byte serach
          }

        }
        printf("Total number of entries = %i\n", total_entries);
        exit(EXIT_SUCCESS);
    }
    int bytes_per_cluster = boot_sector->BPB_SecPerClus * bytes_per_sector;
    //Milestone 4-5

    //Milestone 6: count how many filenames could fit the description
      //print: filename: multiple candidates found

    //Milestone 7: use SHA-1 Hash
    unsigned char num_fats = boot_sector->BPB_NumFATs;    
    if (r_flag){
      int found_candidate = 0;
      
      unsigned int file_size;
      unsigned int update_index;
      
      int bytes_searched = 0;

      char* save_entry;
      int curr_index = boot_sector->BPB_RootClus;

      while (1){

        DirEntry *d = (DirEntry *) dir_entry_addr;

        if (d->DIR_Name[0] == 0x00){
          break;
        }else if (d->DIR_Name[0] == 0xE5){
          char f[13];
          int i; int j;
          for (i = 0; i < 8; i++){
            if (d->DIR_Name[i] == ' '){
              break;
            }
            f[i] = d->DIR_Name[i];
          }

          if (d->DIR_Name[8] != ' '){
            f[i++] = '.';
            for (j = 0; j<3; j++){
              if (d->DIR_Name[8+j] == ' ') break;
              f[i++] = d->DIR_Name[8+j];
            }
            
          }
          f[i] = '\0';
          char* result = (char*)malloc(strlen(f) + 1);
          strcpy(result, f);
          
          if (strcmp(result + 1, filename + 1) == 0) {//filenames match
            unsigned short lo = d->DIR_FstClusLO; 
            unsigned short hi = d->DIR_FstClusHI;
            //if SHA-1, check the SHAsum
            if (s_flag){ 
              //SHA1 computes digest of n bytes at d and places it in md
              unsigned char md[SHA_DIGEST_LENGTH];
              
              //unsigned char user_hash[SHA_DIGEST_LENGTH];
              //hex2bin(sha1, user_hash, SHA_DIGEST_LENGTH);

              unsigned int cluster = (hi << 8) | lo;
              //Calculate the address of the start of the file
              char* file_addr = ( addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
                + ( (cluster - 2)*boot_sector->BPB_SecPerClus) )*bytes_per_sector );

              //compute shasum
              
              SHA1((unsigned char*)file_addr, d->DIR_FileSize, md);

              char sha_string[SHA_DIGEST_LENGTH*2 + 1];
              for (int i = 0; i < SHA_DIGEST_LENGTH; i++){
                sprintf(&sha_string[i*2], "%02x", md[i]);
              }
              //memcmp(md, (unsigned char*)sha1, SHA_DIGEST_LENGTH) != 0
              if (strcmp(sha_string, sha1) != 0){
                
                dir_entry_addr += 32;
                bytes_searched += 32;

                if (bytes_searched == bytes_per_cluster){ //move to next cluster
                  //update pointer to start of next cluster
                  int next_cluster = fat[curr_index];
                  if (next_cluster >= 0xffffff8){
                    break;
                  }else{
                    curr_index = next_cluster;
                    dir_entry_addr = addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
                      + ( (curr_index-2) *boot_sector->BPB_SecPerClus))*bytes_per_sector ;
                  }
                  bytes_searched = 0; //reset byte serach
                }
                continue;
              }

            }

            if (found_candidate && !s_flag){
              printf("%s: multiple candidates found\n", filename);
              exit(EXIT_SUCCESS);
            }
            //store cluster number, store file size
            
            update_index = (hi << 8) | lo;

            file_size = d->DIR_FileSize;
            save_entry = dir_entry_addr;
            found_candidate = 1;
          }
        }

        dir_entry_addr += 32;
        bytes_searched += 32;

        if (bytes_searched == bytes_per_cluster){ //move to next cluster
          //update pointer to start of next cluster
          int next_cluster = fat[curr_index];
          if (next_cluster >= 0xffffff8){
            break;
          }else{
            curr_index = next_cluster;
            dir_entry_addr = addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
              + ( (curr_index-2) *boot_sector->BPB_SecPerClus))*bytes_per_sector ;
          }
          bytes_searched = 0; //reset byte serach
        }
      }

      if (!found_candidate){
        printf("%s: file not found\n", filename);
      }else{
        //Change first letter back to original letter
        DirEntry *update = (DirEntry *) save_entry;
        update->DIR_Name[0] = filename[0];
        //Update ALL FATs
        int bytes_per_cluster = boot_sector->BPB_SecPerClus * bytes_per_sector;
        int cluster_length = file_size / bytes_per_cluster ;
        if (file_size % bytes_per_cluster != 0) {
          cluster_length++;
        }
        for (int i = 0; i < num_fats; i++){
          char* a = fat_addr + (i* boot_sector->BPB_FATSz32 * bytes_per_sector);
          int* fat32 = (int*) a;

          for (int j = 0; j < cluster_length; j++){
            if (j == cluster_length - 1){
              fat32[update_index + j] = 0x0ffffff8;
            }else{
              fat32[update_index + j] = update_index + j + 1;
            }
          }
          
        }
        
        if (s_flag){
          printf("%s: successfully recovered with SHA-1\n", filename);
        }else{
          printf("%s: successfully recovered\n", filename);
        }
        
      }
      
      exit(EXIT_SUCCESS);
    }

    //MILESTONE 8 - Recover a non-contiguously allocated file
    //Assumptions:
      //The entire file is within the first 20 clusters
      //file content occupies no more than 5 clusters

    if (R_flag){
      int bytes_searched = 0;
      int curr_index = boot_sector->BPB_RootClus;

      while (1){

        DirEntry *d = (DirEntry *) dir_entry_addr;

        if (d->DIR_Name[0] == 0x00){
          break;
        }
        else if (d->DIR_Name[0] == 0xE5){
          
          char f[13];
          int i; int j;
          for (i = 0; i < 8; i++){
            if (d->DIR_Name[i] == ' '){
              break;
            }
            f[i] = d->DIR_Name[i];
          }

          if (d->DIR_Name[8] != ' '){
            f[i++] = '.';
            for (j = 0; j<3; j++){
              if (d->DIR_Name[8+j] == ' ') break;
              f[i++] = d->DIR_Name[8+j];
            }
            
          }
          f[i] = '\0';
          char* result = (char*)malloc(strlen(f) + 1);
          strcpy(result, f);
          
          if (strcmp(result + 1, filename + 1) == 0) {//potential candidate, find permutations and compare checksum
            unsigned short lo = d->DIR_FstClusLO; 
            unsigned short hi = d->DIR_FstClusHI;

            unsigned int start_cluster = (hi << 8) | lo;
            unsigned int file_size = d->DIR_FileSize;

            int bytes_per_cluster = boot_sector->BPB_SecPerClus * bytes_per_sector;
            //printf("%i\n", start_cluster);
            int cluster_length = file_size / bytes_per_cluster;
            if (file_size % bytes_per_cluster != 0) {
              cluster_length++;
            }

            char* file_addr = ( addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
                + ( (start_cluster - 2)*boot_sector->BPB_SecPerClus) )*bytes_per_sector );         
            
            if (cluster_length <= 1){ //check if file is max one, same as milestone 7
              unsigned char md[SHA_DIGEST_LENGTH];
              SHA1((unsigned char*)file_addr, file_size, md);

              char sha_string[SHA_DIGEST_LENGTH*2 + 1];
              for (int i = 0; i < SHA_DIGEST_LENGTH; i++){
                sprintf(&sha_string[i*2], "%02x", md[i]);
              }
              
              if (strcmp(sha_string, sha1) != 0){//not match
                
                dir_entry_addr += 32;
                bytes_searched += 32;

                if (bytes_searched == bytes_per_cluster){ //move to next cluster
                  //update pointer to start of next cluster
                  int next_cluster = fat[curr_index];
                  if (next_cluster >= 0xffffff8){
                    break;
                  }else{
                    curr_index = next_cluster;
                    dir_entry_addr = addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
                      + ( (curr_index-2) *boot_sector->BPB_SecPerClus))*bytes_per_sector ;
                  }
                  bytes_searched = 0; //reset byte serach
                }
                continue;
              }else{//found match
                d->DIR_Name[0] = filename[0]; //restore directory entry
                
                //Update ALL FATs
                if (file_size != 0){
                  for (int i = 0; i < num_fats; i++){
                  char* a = fat_addr + (i* boot_sector->BPB_FATSz32 * bytes_per_sector);
                  int* fat32 = (int*) a;

                  fat32[start_cluster] = 0x0ffffff8;
                  }
                }
                
                printf("%s: successfully recovered with SHA-1\n",filename);
                exit(EXIT_SUCCESS);                
              }

            } //end of cluster length 1

            //COMPUTE PERMUTATIONS
                //Iterate through all clusters within cluster 20 (18 itr. since start at 2)
                //make sure to skip if the cluster has already been added to (or equals the start_cluster) permutation -- need to keep track of added cluster alr
                //update SHA has along the way and check sum
                //Keep track of indices to update the fats later, EOF last
                //IF checksum matches:
                  //save important info, update the directory entry, update all FATS, print success
            
            int perm[116280][4]; // array of all permutations
            
            unsigned int numbers[] = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
            unsigned int total_sectors = boot_sector->BPB_TotSec32;
            unsigned int data_sectors = total_sectors - (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs)*(boot_sector->BPB_FATSz32));
            unsigned int data_clusters = (data_sectors / boot_sector->BPB_SecPerClus);
            int num;
            if (data_clusters < 20){
              num = data_clusters ;
            }else{
              num = 20;
            }
            int perm_cnt = 0;
            for (int i = 0; i < num; i++){

              if (numbers[i] == start_cluster){
                  continue;
              }

              for (int j = 0; j < num; j++){

                if (numbers[j] == start_cluster || j == i){
                  continue;
                }

                for (int k = 0; k < num; k++){
                  if (numbers[k] == start_cluster || k == i || k == j){
                    continue;
                  }

                  for (int m = 0; m < num; m++){
                    if (numbers[m] == start_cluster || m == i || m == j || m == k){
                      continue;
                    }
                    perm[perm_cnt][0] = numbers[i];
                    perm[perm_cnt][1] = numbers[j];
                    perm[perm_cnt][2] = numbers[k];
                    perm[perm_cnt][3] = numbers[m];
                    perm_cnt++;

                  }
                }
              }           
            }//End of permutations

            //go through permutations and do check sums 
            //use cluster_length
            for (int i = 0; i < perm_cnt; i++){
              SHA_CTX c;
              SHA1_Init(&c);
              //add first clsuter to SHA
              SHA1_Update(&c, file_addr, bytes_per_cluster);

              int j;
              for (j = 0; j < cluster_length - 1; j++){
                int clst = perm[i][j];
                char* chunk_addr = ( addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
                + ( (clst - 2)*boot_sector->BPB_SecPerClus) )*bytes_per_sector );

                if (j == cluster_length - 2){
                  if (file_size % bytes_per_cluster != 0){
                    SHA1_Update(&c, chunk_addr, file_size % bytes_per_cluster);
                  }else{
                    SHA1_Update(&c, chunk_addr, bytes_per_cluster);
                  }
                  //int n = (file_size % bytes_per_cluster != 0) ? (int) (file_size % bytes_per_cluster) : bytes_per_cluster;
                  //SHA1_Update(&c, chunk_addr, n);
                }else{
                  SHA1_Update(&c, chunk_addr, bytes_per_cluster);
                } 

              }

              //compare hashes
              //SHA1_Final(unsigned char *md, SHA_CTX *c)
              unsigned char md[SHA_DIGEST_LENGTH];
              SHA1_Final(md, &c);
              char sha_string[SHA_DIGEST_LENGTH*2 + 1];
              for (int i = 0; i < SHA_DIGEST_LENGTH; i++){
                sprintf(&sha_string[i*2], "%02x", md[i]);
              }
              
              if (strcmp(sha_string, sha1) == 0){ //hashes match
                
                //update Directory entry
                d->DIR_Name[0] = filename[0];
                //save permutation
                int t = i;
                //UPDATE ALL FATS
                for (int i = 0; i < num_fats; i++){
                  char* a = fat_addr + (i* boot_sector->BPB_FATSz32 * bytes_per_sector);
                  int* fat32 = (int*) a;

                  int fat_ind = start_cluster;
                  int k;
                  for (k = 0; k < cluster_length; k++){
                    if (k == cluster_length - 1){
                      fat32[fat_ind] = 0x0ffffff8; //EOF
                    }else{
                      fat32[fat_ind] = perm[t][k];
                    }
                    fat_ind = perm[t][k];
                  }
 
                }
                //print success and exit
                printf("%s: successfully recovered with SHA-1\n",filename);
                exit(EXIT_SUCCESS);  
              } 
            } //end of permutation loop

          }

          
        }
        //MOVE TO NEXT DIRECTORY ENTRY
        dir_entry_addr += 32;
        bytes_searched += 32;

        if (bytes_searched == bytes_per_cluster){ //move to next cluster
          //update pointer to start of next cluster
          int next_cluster = fat[curr_index];
          if (next_cluster >= 0xffffff8){
            break;
          }else{
            curr_index = next_cluster;
            dir_entry_addr = addr + (boot_sector->BPB_RsvdSecCnt + (boot_sector->BPB_NumFATs * boot_sector->BPB_FATSz32)
              + ( (curr_index-2) *boot_sector->BPB_SecPerClus))*bytes_per_sector ;
          }
          bytes_searched = 0; //reset byte search
        }

    }//end of while loop
      
    //Nothing found
    printf("%s: file not found\n", filename);
    exit(EXIT_SUCCESS);
  }

  return 0;
   
//print out usage information
usage: 
  printf("Usage: %s disk <options>\n", argv[0]);
  printf("  -i                     Print the file system information.\n");
  printf("  -l                     List the root directory.\n");
  printf("  -r filename [-s sha1]  Recover a contiguous file.\n");
  printf("  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
  return 1;

}


