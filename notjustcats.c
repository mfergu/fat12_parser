//http://www.dfists.ua.es/~gil/FAT12Description.pdf
//http://minirighi.sourceforge.net/html/fat12_8c.html
/*
 ________   ________  _________           ___  ___  ___  ________  _________   
|\   ___  \|\   __  \|\___   ___\        |\  \|\  \|\  \|\   ____\|\___   ___\ 
\ \  \\ \  \ \  \|\  \|___ \  \_|        \ \  \ \  \\\  \ \  \___|\|___ \  \_| 
 \ \  \\ \  \ \  \\\  \   \ \  \       __ \ \  \ \  \\\  \ \_____  \   \ \  \  
  \ \  \\ \  \ \  \\\  \   \ \  \     |\  \\_\  \ \  \\\  \|____|\  \   \ \  \ 
   \ \__\\ \__\ \_______\   \ \__\    \ \________\ \_______\____\_\  \   \ \__\
    \|__| \|__|\|_______|    \|__|     \|________|\|_______|\_________\   \|__|
                                                           \|_________|        
                                                                               
                                                                               
                 ________  ________  _________  ________                       
                |\   ____\|\   __  \|\___   ___\\   ____\                      
                \ \  \___|\ \  \|\  \|___ \  \_\ \  \___|_                     
                 \ \  \    \ \   __  \   \ \  \ \ \_____  \                    
                  \ \  \____\ \  \ \  \   \ \  \ \|____|\  \                   
                   \ \_______\ \__\ \__\   \ \__\  ____\_\  \                  
                    \|_______|\|__|\|__|    \|__| |\_________\                 
                                                  \|_________|                 
                                                                               
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define CLUSTER_SIZE 512
#define CLUSTERS_PER_PAGE 8

typedef struct directory_t {
	unsigned char	filename[8];
	unsigned char	ext[3];
	unsigned char	attrib;
	unsigned short	reserved;
	unsigned short	time_created;
	unsigned short	date_created;
	unsigned short	date_last_accessed;
	unsigned short	ignore;
	unsigned short	last_write_time;
	unsigned short	last_write_date;
	unsigned short	first_logical_cluster;
	unsigned int	file_size;
} directory_t;

typedef enum attribute_t {
	deleted_file =		0xE5,
	padding =			0x20,
	last_cluster_in_file =	0xFF8,
	free_dir =			0x00,
	read_only_attr =	0x01,
	hidden_attr =		0x02,
	system_attr =		0x04,
	volume_attr =		0x08,
	subdir_attr =		0x10,
} attribute_t;
	
		
unsigned int *fixFat( unsigned char *);
void traverseDirectory( unsigned char *, unsigned int *, unsigned int, unsigned int, char *, unsigned int, signed char);
void printToOut( unsigned char *, unsigned char *, unsigned char *, unsigned int, unsigned int, unsigned char);

unsigned int *fixFat( unsigned char *old_fat) {
	// 0x12, 0x34, 0x56 ~~~> 0x412 and 0x563
	unsigned int *new_fat, fat_size = 3072;
	int fd= -1;

	new_fat = (unsigned int *) mmap(NULL, PAGE_SIZE*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, fd, 0);

	for( unsigned int i = 0, j = 0; i < fat_size; i+=3 ) {
		new_fat[j++] = ( old_fat[i] + ( old_fat[i+1] << 8)) & 0x0FFF;
		new_fat[j++] = ( old_fat[i+1] + ( old_fat[i+2] << 8)) >> 4;
	}
	return new_fat;
}

void printFileName( unsigned char *file_name, unsigned int num_chars) {
		
	if( file_name == NULL) 
		return;

	for( unsigned int i = 0; i < num_chars; i++ ) {
		if( *(file_name + i) == free_dir) {
			 break;
		} else if(*((unsigned char *) (file_name + i)) == deleted_file) {
			printf("_");
		} else if( *(file_name + i) != padding) {
			printf("%c", *(file_name + i));
		}
	}
}

void printToOut(unsigned char *file_name, unsigned char *file_path, unsigned char *ext, unsigned int num_path_chars, unsigned int file_size, unsigned char deleted) {

	printf("FILE\t");
	printf("%s", deleted? "DELETED\t" : "NORMAL\t");
	printFileName( file_path, num_path_chars);
	printf("/");
	printFileName(file_name, 8);
	printf(".");
	printFileName(ext, 3);
	printf("\t%u\n", file_size);
	
}

char *input_file_path = NULL, *output_file_path = NULL;
int fd, input_path_length, output_path_length, files_printed;

int main( int argc, char** argv) {

	unsigned char	*sector = NULL, 
					*file_name = NULL,
					*root_dir = NULL,
					*file_path = NULL,
					*old_fat = NULL;

	unsigned int *fixed_fat = NULL;
	
	files_printed = 0;
	input_path_length = strlen(argv[1]);
	output_path_length = strlen(argv[2]);

	input_file_path = (unsigned char*) calloc( input_path_length ,sizeof(char));
	strcpy( input_file_path, argv[1]);
	fd = open( input_file_path, O_RDONLY);

	output_file_path = (unsigned char*) calloc( output_path_length, sizeof(char));
	strcpy( output_file_path, argv[2]);

	sector = mmap( NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	old_fat = (unsigned char *) sector + CLUSTER_SIZE;

	fixed_fat = fixFat(old_fat);	

	root_dir = mmap(NULL, PAGE_SIZE*2, PROT_READ, MAP_SHARED, fd, PAGE_SIZE*2);
	root_dir += 3 * CLUSTER_SIZE;

	traverseDirectory( root_dir, fixed_fat, 0, -1, file_path, 0, 0);

	munmap( root_dir = 3*CLUSTER_SIZE, PAGE_SIZE*2);
	munmap( fixed_fat, 3072);

	return 0;
}

void traverseDirectory( unsigned char *directory, unsigned int *fat, unsigned int current_entry, unsigned int parent_entry, char *file_path, unsigned int num_chars, signed char is_deleted) {

	unsigned char *current_directory = directory;
	unsigned char *current_byte = (unsigned char*) directory;
	int offset = 0;

	// cycle through fat entries 
	while(*current_byte != free_dir) {
	
		//found a directory
		directory_t *temp_dir = (directory_t *) current_byte;
 	
		// check dir attributes; is the file size zero and is it a subdirectory
		if( temp_dir->file_size == free_dir && (temp_dir->attrib & subdir_attr) == subdir_attr) {
			//check if current directory is parent or current
			if( temp_dir->first_logical_cluster != current_entry && temp_dir->first_logical_cluster != parent_entry) {

				//get child directory location
				unsigned char next_entry = temp_dir->first_logical_cluster;
				unsigned int next_sector = ( 33 + next_entry - 2);

	//new_dir = (unsigned char *) mmap(NULL, PAGE_SIZE*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, fd, 0);
				unsigned char *found_directory = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, 
					fd, (next_sector / CLUSTERS_PER_PAGE) * PAGE_SIZE);
				found_directory += (next_sector % 8) * CLUSTER_SIZE;

				unsigned char * new_file_path = calloc((num_chars+20), sizeof(char));

				if(file_path != NULL) {
					strcpy( new_file_path, file_path);
				}
			
				// add path data to file path
				strcat( new_file_path, "/");
				strncat( new_file_path, (char*) current_byte, 8);	
//				printf("%s\n", new_file_path);

// traverseDirectory( *directory, *fat, current_entry, parent_entry, *file_path, num_chars, is_deleted) {
				// if marked for deletion recover lost files 
				traverseDirectory( found_directory, fat, next_entry, current_entry, new_file_path, num_chars + 20,\
					*current_byte == deleted_file ? 1 : 0);

				found_directory -= (next_sector%8)*CLUSTER_SIZE;
				munmap(found_directory, PAGE_SIZE);
				
			}
		} else if ( *current_byte == deleted_file || is_deleted == 1) {
			printToOut( temp_dir->filename, file_path, temp_dir->ext, num_chars, temp_dir->file_size, 1);
		} else {
			printToOut( temp_dir->filename, file_path, temp_dir->ext, num_chars, temp_dir->file_size, 0);
		}

		//move forward 32 bytes to the next entry
		// count these bytes as already read
		current_byte += sizeof(directory_t);
		offset += sizeof(directory_t);
		
		//if offset is 512, map new sectors and unmap current sectors
		if( offset == CLUSTER_SIZE) {
			if( current_entry != 0 ) {
				//if this isn't the root directory 
				//check to see where the next sector is 
				if( current_entry == free_dir || current_entry >= last_cluster_in_file) {
					return;

				} else {
					int next_entry = fat[current_entry];
					int sector_num = (33 + next_entry - 2);
					//unmap currentDirectory
					current_byte = mmap( NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd,\
						(sector_num/CLUSTERS_PER_PAGE)*PAGE_SIZE + (sector_num%8)*CLUSTER_SIZE);	
					current_directory = (char*) current_byte;
				}
			}
			offset = 0;
		}
	}
}
		











