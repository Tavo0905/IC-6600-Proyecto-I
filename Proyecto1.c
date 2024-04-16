#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define MSGSZ 2048
#define MAX_PATH_SIZE 1024

struct msg_buffer {
    long msg_type;
    char msg_text[MSGSZ];
};

void copy_file(const char *src, const char *dst, FILE *logfile, int thread_id) {

    clock_t startP, endP;

    startP = clock();

    FILE *source = fopen(src, "r");
    if (source == NULL) { // En caso de no existir el archivo
        perror("Error opening source file");
        exit(EXIT_FAILURE);
    }

    FILE *dest = fopen(dst, "w");
    if (dest == NULL) { // En caso de no encontrar el destino
        perror("Error opening destination file");
        fclose(source);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFSIZ];
    size_t bytes;

    while ((bytes = fread(buffer, 1, BUFSIZ, source)) > 0) {
        // Copia el archivo al destino
        fwrite(buffer, 1, bytes, dest);
    }

    fclose(source);
    fclose(dest);

    endP = clock();
    // Registrar en el archivo de bitácora
    fprintf(logfile, "%s,%d,%f\n", src, thread_id, (double) (endP - startP) / CLOCKS_PER_SEC);
}

void copy_directory(const char *src, const char *dst, FILE *logfile, int thread_id) {
    DIR *dir;
    struct dirent *dp;

    if ((dir = opendir(src)) == NULL) { // En caso de no encontrar el directorio
        perror("Error opening source directory");
        exit(EXIT_FAILURE);
    }

    char src_path[MAX_PATH_SIZE], dst_path[MAX_PATH_SIZE];

    while ((dp = readdir(dir)) != NULL) {

        // Evitamos reprocesar el directorio actual y el anterior
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src, dp->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, dp->d_name);

        struct stat statbuf;
        stat(src_path, &statbuf);

        if (S_ISDIR(statbuf.st_mode)) {
            // Si encuentra otro directorio lo vuelve a copiar
            if (mkdir(dst_path, 0777) == -1) {
                perror("Error creating destination directory");
                exit(EXIT_FAILURE);
            }
            printf("Copying directory: %s, Size: %ld bytes\n", src_path, statbuf.st_size);
            copy_directory(src_path, dst_path, logfile, thread_id);
        } else {
            // Si encuentra un archivo lo copia
            printf("Copying file: %s, Size: %ld bytes\n", src_path, statbuf.st_size);
            copy_file(src_path, dst_path, logfile, thread_id);
        }
    }

    closedir(dir);
}

void worker(int msqid, FILE *logfile, int thread_id) {

    // Para cada uno de los procesos hijos
    struct msg_buffer message;
    char src_path[MAX_PATH_SIZE], dst_path[MAX_PATH_SIZE];

    while (1) {
        if (msgrcv(msqid, &message, MSGSZ, 1, 0) == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }

        if (strcmp(message.msg_text, "DONE") == 0) {
            break;
        }

        sscanf(message.msg_text, "%s %s", src_path, dst_path);

        struct stat statbuf;
        stat(src_path, &statbuf);
        
        // Valida si es un dir o un file, y luego realiza la copia
        if (S_ISDIR(statbuf.st_mode)) {
            if (mkdir(dst_path, 0777) == -1) {
                perror("Error creating destination directory");
                exit(EXIT_FAILURE);
            }
            printf("Copying directory: %s, Size: %ld bytes\n", src_path, statbuf.st_size);
            copy_directory(src_path, dst_path, logfile, thread_id);
        } else {
            printf("Copying file: %s, Size: %ld bytes\n", src_path, statbuf.st_size);
            copy_file(src_path, dst_path, logfile, thread_id);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source_directory> <destination_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    clock_t start, end;

    // Apertura del archivo de bitácora
    FILE *logfile = fopen("logfile.csv", "w");
    if (logfile == NULL) {
        perror("Error opening logfile");
        exit(EXIT_FAILURE);
    } else {
        fprintf(logfile, "File,Thread,Time\n");
    }


    // Se extraen los directorios objetivo y destino
    char *src_dir = argv[1];
    char *dst_dir = argv[2];

    DIR *dir;
    struct dirent *dp;
    struct msg_buffer message;
    int msqid;

    key_t key = ftok("msgq", 65);
    msqid = msgget(key, 0666 | IPC_CREAT);

    int pid;
    int num_workers = 4;

    start = clock(); // Toma el tiempo inicial para sacar el total

    printf("\n");

    for (int i = 0; i < num_workers; i++) {
        pid = fork();
        if (pid == 0) {
            worker(msqid, logfile, i);
            exit(EXIT_SUCCESS);
        }
    }

    if ((dir = opendir(src_dir)) == NULL) {
        perror("Error opening source directory");
        exit(EXIT_FAILURE);
    }

    char src_path[MAX_PATH_SIZE], dst_path[MAX_PATH_SIZE];

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, dp->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dp->d_name);

        snprintf(message.msg_text, sizeof(message.msg_text), "%s %s", src_path, dst_path);
        message.msg_type = 1;

        if (msgsnd(msqid, &message, strlen(message.msg_text) + 1, 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
    }

    closedir(dir);

    for (int i = 0; i < num_workers; i++) {
        snprintf(message.msg_text, sizeof(message.msg_text), "DONE");
        message.msg_type = 1;

        if (msgsnd(msqid, &message, strlen(message.msg_text) + 1, 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_workers; i++) {
        wait(NULL);
    }

    // Se calcula la diferencia de tiempos
    end = clock();
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;

    msgctl(msqid, IPC_RMID, NULL);

    printf("Directory copied successfully!\n\n");

    printf("Time taken to copy file/directory: %f seconds\n", cpu_time_used);

    fclose(logfile);

    return 0;
}
