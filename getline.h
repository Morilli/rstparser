#include <stdio.h>

ssize_t _getline(char** lineptr, size_t* n, FILE* stream);

#define getline(lineptr, n, stream) _getline(lineptr, n, stream)
