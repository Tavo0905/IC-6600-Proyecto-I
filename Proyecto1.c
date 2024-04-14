#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

void copy_file(const char *src, const char *dst) {
    FILE *source = fopen(src, "r");
    if (source == NULL) {
        perror("Error opening source file");
        exit(EXIT_FAILURE);
    }

    FILE *dest = fopen(dst, "w");
    if (dest == NULL) {
        perror("Error opening destination file");
        fclose(source);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFSIZ];
    size_t bytes;

    while ((bytes = fread(buffer, 1, BUFSIZ, source)) > 0) {
        fwrite(buffer, 1, bytes, dest);
    }

    fclose(source);
    fclose(dest);
}

void copy_directory(const char *src_dir, const char *dst_dir) {
    DIR *dir;
    struct dirent *dp;
    struct stat st;
    char src_path[512], dst_path[512];

    if ((dir = opendir(src_dir)) == NULL) {
        perror("Error opening source directory");
        exit(EXIT_FAILURE);
    }

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, dp->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dp->d_name);

        if (stat(src_path, &st) == -1) {
            perror("Error getting file status");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Es un directorio
            if (mkdir(dst_path, st.st_mode) == -1) {
                perror("Error creating directory");
            }
            copy_directory(src_path, dst_path);
        } else {
            // Es un archivo
            copy_file(src_path, dst_path);
        }
    }

    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source_directory> <destination_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *src_dir = argv[1];
    char *dst_dir = argv[2];

    copy_directory(src_dir, dst_dir);

    printf("Directory copied successfully!\n");

    return 0;
}
