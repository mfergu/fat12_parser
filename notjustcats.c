//http://www.dfists.ua.es/~gil/FAT12Description.pdf
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
	free_dir =			0x00,
	read_only_attr =	0x01,
	hidden_attr =		0x02,
	system_attr =		0x04,
	volume_attr =		0x08,
	subdir_attr =		0x10,
} attribute_t;
	
		
void traverseDirectory( char *, unsigned int *, int, int, char *, int, int);
unsigned int *fixFat( unsigned char *);
void writeOutFiles( unsigned int *, char *);
void printFileName( char *, int);

unsigned int *fixFat( unsigned char *old_fat) {
	// 0x12, 0x34, 0x56 ~~~> 0x412 and 0x563
	unsigned int *new_fat, fat_size = 3072;
	unsigned char nibble = 0;
	int fd= -1;

	new_fat = (unsigned char *) mmap(NULL, PAGE_SIZE*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, fd, 0);

	for( int i = 0, j = 0; i < fat_size; i+=3 ) {
		new_fat[j++] = ( old_fat[i] + ( old_fat[i+1] << 8)) & 0x0FFF;
		new_fat[j++] = ( old_fat[i+1] + ( old_fat[i+2] << 8)) >> 4;
	}
	return new_fat;
}

// 8 bytes max size name
void printFileName( char *file_name, int num_chars) {
		
	if( file_name == NULL) 
		return;

	for( int i = 0; i < num_chars; i++ ) {
		// if file name is marked as free all remaining dirs in this dir are free
		if( file_name[i] == free_dir) {
			 break;
		// file name is marked as free
		} else if(*((unsigned char *) (file_name + i)) == deleted_file) {
			printf("_");

		} else if( file_name[i] != padding) {
			printf("%c", file_name[i]);
		}
	}
}

char *input_file_path = NULL, *output_file_path = NULL;
int fd, input_path_length, output_path_length, files_printed;

int main( int argc, char** argv) {

	unsigned char	*chunk = NULL, 
					*file_name = NULL,
					*root_dir = NULL,
					*file_path = NULL,
					*fat = NULL;

	unsigned int *fixed_fat = NULL;
	
	files_printed = 0;
	input_path_length = strlen(argv[1]);
	output_path_length = strlen(argv[2]);

	input_file_path = (char*) calloc( input_path_length ,sizeof(char));
	strcpy( input_file_path, argv[1]);
	fd = open( input_file_path, O_RDONLY);

	output_file_path = (char*) calloc( output_path_length, sizeof(char));
	strcpy( output_file_path, argv[2]);

	chunk = mmap( NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	fat = (unsigned char *) chunk + CLUSTER_SIZE;

	fixed_fat = fixFat(fat);	

	root_dir = mmap(NULL, PAGE_SIZE*2, PROT_READ, MAP_SHARED, fd, PAGE_SIZE*2);
	root_dir += 3 * CLUSTER_SIZE;

	traverseDirectory( root_dir, fixed_fat, 0, -1, file_path, 0, 0);

	munmap( root_dir = 3*CLUSTER_SIZE, PAGE_SIZE*2);

	return 0;
}

void traverseDirectory( char *directory, unsigned int *fat, int current_entry, int parent_entry, char *file_path,	 int num_chars, int is_deleted) {

	char* current_directory = directory;
	unsigned char *current_byte = (unsigned char*) directory;
	int current_bytes = 0;

	// cycle through fat entries 
	while(*current_byte != free_dir) {
	
		//found a directory
		directory_t *temp_dir = (directory_t *) current_byte;
 	
		// check dir attributes; is it a subdirectory and is the file size zero 
		if( ( temp_dir->attrib & subdir_attr) == subdir_attr && temp_dir->file_size == free_dir) {
			//check if current directory is parent or current
			if( temp_dir->first_logical_cluster != current_entry && temp_dir->first_logical_cluster != parent_entry) {

				//get child directory location
				unsigned char new_entry = temp_dir->first_logical_cluster;
				//physical sector number  = 33 + fat entry number - 2
				int new_sector = ( 33 + new_entry - 2);

				char *found_directory = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, 
					fd, (new_sector / CLUSTERS_PER_PAGE)*PAGE_SIZE);
				found_directory += (new_sector%8)*CLUSTER_SIZE;

				char * new_file_path = calloc((num_chars+20), sizeof(char));

				if(file_path != NULL) {
					strcpy( new_file_path, file_path);
				}
			
				// add path data to file path
				strcat( new_file_path, "/");
				strncat( new_file_path, (char*) current_byte, 8);	

				// if marked for deletion recover lost files 
				if( *current_byte == deleted_file) {
					traverseDirectory( found_directory, fat, new_entry, current_entry, new_file_path, num_chars + 20, 1);
					found_directory -= (new_sector%8)*CLUSTER_SIZE;
					munmap(found_directory, PAGE_SIZE);
				// recover files 
				} else {
					traverseDirectory( found_directory, fat, new_entry, current_entry, new_file_path, num_chars+20, 0);
					found_directory -= (new_sector%8)*CLUSTER_SIZE;
					munmap(found_directory, PAGE_SIZE);
				}
			}
		// if the dir has been marked for deletion
		} else if ( *current_byte == deleted_file || is_deleted == 1) {

			printf("FILE\tDELETED\t");
			printFileName( file_path, num_chars);
			printf("/");
			printFileName( temp_dir->filename, 8);
			printf(".");
			printFileName( temp_dir->ext, 3);
			printf("\t%d\n", temp_dir->file_size);
			writeOutFiles(fat, (char *) current_byte);

		} else {
			
			printf("FILE\tNORMAL\t");
			printFileName( file_path, num_chars);
			printf("/");
			printFileName( temp_dir->filename, 8);
			printf(".");
			printFileName( temp_dir->ext, 3);
			printf("\t%d\n", temp_dir->file_size);
			writeOutFiles( fat, temp_dir);
		}

		//move forward 32 bytes to the next entry
		// count these bytes as already read
		current_byte += 32;
		current_bytes += 32;
		
		//if current bytes is 512, map new sectors and unmap current sectors
		if( current_bytes == CLUSTER_SIZE) {
			if( current_entry != 0 ) {
				//if this isn't the root directory 
				//check to see where the next sector is 
				if( current_entry == free_dir || current_entry >= 0xFF8) {
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
		current_bytes = 0;
		}
	}
}
		
void writeOutFiles( unsigned int *fat, char *directory_entry) {

	char *out_file_name = (char *) calloc( input_path_length, sizeof(char)); // the full path including the file name
	char *found_file = (char *) calloc( 10, sizeof(char)); // the 8 char file name

	sprintf( found_file, "file%d.%c%c%c", files_printed, *(directory_entry+8), *(directory_entry+9), *(directory_entry+10));
	files_printed++;

	strcpy( out_file_name, output_file_path);
	out_file_name = strcat(out_file_name, "/");
	out_file_name = strcat(out_file_name, found_file);

	int new_file = open(out_file_name, O_WRONLY | O_CREAT, S_IRWXO | S_IRWXU);
	int size = *((int *) (directory_entry + 28));
	int bytes_printed = 0;
	
	//get the number of the current cluster
	int fat_num = *((short *) (directory_entry + 26));
	int sector_num = 33 + fat_num - 2;

	//map mem
	char *file_data;

	do {
		//grab new mem
		file_data = (char*) mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
			(sector_num / CLUSTERS_PER_PAGE)*PAGE_SIZE);
		file_data += (sector_num % 8) * CLUSTER_SIZE;

		if( size - bytes_printed < CLUSTER_SIZE) {
			write( new_file, file_data, size - bytes_printed);
			bytes_printed = size;
		} else {
			write( new_file, file_data, CLUSTER_SIZE);
			bytes_printed += CLUSTER_SIZE;
		}

		if( fat[fat_num] == 0x00) {
			fat_num++;
		} else {
			fat_num = fat[fat_num];
		}
		sector_num = 33 + fat_num - 2;
		//while this number is not above 0xff8
	} while( fat_num < 0xFF8 && bytes_printed < size);

	close(new_file);
}










































