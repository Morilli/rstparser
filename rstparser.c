#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <inttypes.h>
#include <assert.h>

#include "static_list.h"
#include "list.h"
#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#if _POSIX_C_SOURCE < 200809L && !defined(_GNU_SOURCE)
    #include "getline.h"
#endif

typedef struct _stringHash {
    uint64_t hash;
    char* string;
} StringHash;
typedef LIST(StringHash) StringHashList;

typedef struct _rstEntry {
    char* string;
    uint64_t offset_and_hash;
} RstEntry;
typedef STATIC_LIST(RstEntry) RstEntryList;

typedef struct _rstFile {
    uint8_t version;
    uint8_t hash_bits;
    char* font_config;
    RstEntryList entries;
} RstFile;

RstFile* parse_rst_file(const char* input_path)
{
    FILE* inputFile = fopen(input_path, "rb");
    if (!inputFile) {
        fprintf(stderr, "Error: Failed to open input file.\n");
        return NULL;
    }
    fseek(inputFile, 0, SEEK_END);
    int fileSize = ftell(inputFile);
    rewind(inputFile);
    if (fileSize < 9) {
        fileTruncated:
        fprintf(stderr, "Error: Input file truncated.\n");
        fclose(inputFile);
        return NULL;
    }

    RstFile* rstFile = malloc(sizeof(RstFile));
    rstFile->font_config = NULL;
    rstFile->hash_bits = 40;
    char magic[3];
    assert(fread(magic, 1, 3, inputFile) == 3);
    if (memcmp(magic, "RST", 3) != 0) {
        fprintf(stderr, "Error: Input not an RST file.\n");
        fclose(inputFile);
        return NULL;
    }
    rstFile->version = getc(inputFile);
    switch(rstFile->version)
    {
        case 2: {
            if (getc(inputFile) > 0) {
                uint32_t length;
                assert(fread(&length, 4, 1, inputFile) == 1);
                rstFile->font_config = malloc(length + 1);
                assert(fread(rstFile->font_config, 1, length, inputFile) == length);
                rstFile->font_config[length] = '\0';
            }
            break;
        }
        case 3: break;
        case 4:
        case 5:
            rstFile->hash_bits = 39;
            break;
        default:
            fprintf(stderr, "Error: Unsupported RST version %d\n.", rstFile->version);
            fclose(inputFile);
            return NULL;
    }

    uint32_t entry_count;
    assert(fread(&entry_count, 4, 1, inputFile) == 1);
    initialize_static_list(&rstFile->entries, entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        assert(fread(&rstFile->entries.objects[i].offset_and_hash, 8, 1, inputFile) == 1);
    }
    if (rstFile->version < 5)
        fseek(inputFile, 1, SEEK_CUR);
    size_t data_size = fileSize - ftell(inputFile);
    uint8_t* data = malloc(data_size);
    assert(fread(data, 1, data_size, inputFile) == data_size);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t data_offset = rstFile->entries.objects[i].offset_and_hash >> rstFile->hash_bits;
        if (data_size <= data_offset) goto fileTruncated;
        rstFile->entries.objects[i].string = strdup((char*) data + data_offset);
    }
    free(data);

    fclose(inputFile);
    return rstFile;
}

void save_rst_file(const RstFile* rstFile, const char* output_path, StringHashList* hashes)
{
    FILE* outputFile = fopen(output_path, "wb");
    if (!outputFile) {
        fprintf(stderr, "Error: Failed to open output file \"%s\"\n", output_path);
        return;
    }

    if (rstFile->font_config) {
        fputs(rstFile->font_config, outputFile);
    }

    for (uint32_t i = 0; i < rstFile->entries.length; i++) {
        if (hashes) {
            StringHash* stringHash = NULL;
            find_object_s(hashes, stringHash, hash, rstFile->entries.objects[i].offset_and_hash & ((1ull << rstFile->hash_bits) - 1));
            if (stringHash) {
                char buffer[sizeof("tr \"\" = \"\"\n") + strlen(stringHash->string) + strlen(rstFile->entries.objects[i].string)];
                sprintf(buffer, "tr \"%s\" = \"%s\"\n", stringHash->string, rstFile->entries.objects[i].string);
                fputs(buffer, outputFile);
                continue;
            }
        }
        char buffer[sizeof("tr \"0123456789ABCDEF\" = \"\"\n") + strlen(rstFile->entries.objects[i].string)];
        sprintf(buffer, "tr \"%"PRIu64"\" = \"%s\"\n", rstFile->entries.objects[i].offset_and_hash & (((uint64_t) 1 << rstFile->hash_bits) - 1), rstFile->entries.objects[i].string);
        fputs(buffer, outputFile);
    }
    fclose(outputFile);
}

StringHashList* load_rst_hashes(const char* path, int hash_bits)
{
    FILE* hashesFile = fopen(path, "rb");
    if (!hashesFile) {
        fprintf(stderr, "Warning: Failed to open hashfile \"%s\"\n", path);
        return NULL;
    }

    StringHashList* hashList = malloc(sizeof(StringHashList));
    initialize_list(hashList);
    char* currentLine = NULL;
    size_t allocatedLength = 0;
    ssize_t lineLength;
    while ( (lineLength = getline(&currentLine, &allocatedLength, hashesFile)) != -1) {
        if (currentLine[lineLength-1] == '\n') currentLine[--lineLength] = '\0';
        if (lineLength > 0 && currentLine[lineLength-1] == '\r') currentLine[--lineLength] = '\0'; // windows line endings urgh
        char* afterHash;
        uint64_t hash = strtoull(currentLine, &afterHash, 16);
        char* name;
        if (afterHash == currentLine || *afterHash != ' ') { // heuristic detection of string only lines, should work 100%
            name = currentLine;
            hash = XXH64(currentLine, lineLength, 0);
        } else {
            name = afterHash;
            while (*name == ' ') name++;
        }
        add_object(hashList, (&(StringHash) {.hash = hash & ((1ull << hash_bits) - 1), .string = strdup(name)}));
    }
    free(currentLine);
    sort_list(hashList, hash);

    fclose(hashesFile);
    return hashList;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Error: Input file needed.\nSyntax: rstparser path/to/input.file [path/to/output.file]\n");
        exit(EXIT_FAILURE);
    }

    char* input_file = argv[1];
    char* output_file;
    if (argc >= 3) {
        output_file = argv[2];
    } else {
        int input_length = strlen(argv[1]);
        output_file = alloca(input_length + 5);
        memcpy(output_file, input_file, input_length);
        memcpy(output_file + input_length, ".txt", 5);
    }

    RstFile* parsed_rst_file = parse_rst_file(input_file);
    if (!parsed_rst_file) {
        fprintf(stderr, "Error: Failed to parse input file \"%s\"\n", input_file);
        exit(EXIT_FAILURE);
    }
    StringHashList* hashes = load_rst_hashes("hashes.rst.txt", parsed_rst_file->hash_bits);
    save_rst_file(parsed_rst_file, output_file, hashes);

    // cleanup
    if (hashes) {
        for (uint32_t i = 0; i < hashes->length; i++) {
            free(hashes->objects[i].string);
        }
        free(hashes->objects);
        free(hashes);
    }
    for (uint32_t i = 0; i < parsed_rst_file->entries.length; i++) {
        free(parsed_rst_file->entries.objects[i].string);
    }
    free(parsed_rst_file->entries.objects);
    free(parsed_rst_file->font_config);
    free(parsed_rst_file);
}
