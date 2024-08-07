#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TOKEN_LEN 8
#define TOKEN_SET_LEN ((uint8_t)1 << TOKEN_LEN)
#define READ_CHUNK_SIZE 100000
#define WRITE_CHUNK_SIZE 100000
#define NUM_BYTES(bits) ((bits - 1) / 8 + 1)

/* improvements:
 * - multithread in calc_char_freqs
 * - multithread in file writing?
 * - add appropriate keywords (const, static, etc)
 */

typedef _Bool bool;

typedef struct Node {
  struct Node *l, *r;
  uint64_t count;
  uint8_t token;
  bool is_leaf;
} Node;

typedef struct CharCode {
  uint64_t code;
  uint8_t code_len;
  uint8_t token;
} CharCode;

uint16_t num_chars = 0;

void *safemalloc(size_t size, const char *err_msg) {
  void *m = malloc(size);
  if (!m) {
    fprintf(stderr, "%s", err_msg);
    exit(1);
  }
  return m;
}

void *safecalloc(size_t count, size_t size, const char *err_msg) {
  void *arr = calloc(count, size);
  if (!arr) {
    fprintf(stderr, "%s", err_msg);
    exit(1);
  }
  return arr;
}

uint64_t *calculate_char_freqs(FILE *f) {
  uint64_t *freq_arr =
      (uint64_t *)safecalloc((size_t)TOKEN_SET_LEN, sizeof(uint64_t),
                             "char frequency allocation failed\n");

  char *s = "failed initializing char arr in char freqs\n";
  uint8_t *read_chunk = safecalloc(READ_CHUNK_SIZE, 1, s);

  size_t bytes_read = 0;
  while ((bytes_read = fread(read_chunk, 1, READ_CHUNK_SIZE, f)) > 0) {
    for (size_t i = 0; i < bytes_read; i++) {
      freq_arr[read_chunk[i]]++;
    }
  }

  return freq_arr;
}

uint16_t get_num_chars(uint64_t *freq_arr) {
  uint16_t num_chars = 0;
  for (int i = 0; i < TOKEN_SET_LEN; i++)
    if (freq_arr[i] != 0)
      ++num_chars;
  return num_chars;
}

Node *init_node(Node *n1, Node *n2, uint8_t tkn, uint64_t cnt, bool is_leaf) {
  Node *N = (Node *)safemalloc(sizeof(Node), "failure initializing Node\n");

  N->l = n1;
  N->r = n2;
  N->token = tkn;
  N->count = cnt;
  N->is_leaf = is_leaf;
  return (Node *)N;
}

CharCode *init_charcode(uint64_t code, uint8_t code_len, uint8_t token) {
  char *s = "failure initializing CharCode\n";
  CharCode *C = (CharCode *)safemalloc(sizeof(CharCode), s);

  C->code = code;
  C->code_len = code_len;
  C->token = token;
  return C;
}

Node **init_node_arr_from_chars(uint64_t *freq_arr, uint16_t num_chars) {
  char *s = "failure initializing node array\n";
  Node **node_arr = (Node **)safecalloc(num_chars, sizeof(Node *), s);

  uint64_t j = 0;
  for (uint64_t i = 0; i < TOKEN_SET_LEN; i++) {
    if (freq_arr[i] != 0) {
      node_arr[j] = init_node(NULL, NULL, i, freq_arr[i], 1);
      ++j;
    }
  }
  return node_arr;
}

void swap_idxs(Node **node_arr, uint64_t lidx, uint64_t slidx,
               uint64_t max_idx) {
  // swap 1
  Node *ldx_tmp = node_arr[slidx];
  node_arr[lidx] = node_arr[max_idx - 1];
  node_arr[max_idx - 1] = ldx_tmp;
  // swap 2
  if (slidx == max_idx - 1)
    // slidx has been swapped by code above, so now in lidx
    slidx = lidx;
  Node *slidx_tmp = node_arr[slidx];
  node_arr[slidx] = node_arr[max_idx - 2];
  node_arr[max_idx - 2] = slidx_tmp;
}

Node **get_min_two(Node **node_arr, uint64_t max_idx) {
  uint64_t lowest = UINT_MAX;
  uint64_t second_lowest = UINT_MAX;
  uint64_t lidx = UINT_MAX;
  uint64_t slidx = UINT_MAX;

  char *s = "failure initializing node array in get_min_two\n";
  Node **lowest_pair = (Node **)safecalloc(2, sizeof(Node *), s);

  for (uint64_t i = 0; i < max_idx; i++) {
    if (node_arr[i]->count < lowest) {
      if (lowest < second_lowest) {
        second_lowest = lowest;
        lowest_pair[1] = lowest_pair[0];
        slidx = lidx;
      }

      lowest = node_arr[i]->count;
      lowest_pair[0] = node_arr[i];
      lidx = i;
    } else if (node_arr[i]->count < second_lowest) {
      second_lowest = node_arr[i]->count;
      lowest_pair[1] = node_arr[i];
      slidx = i;
    }
  }
  swap_idxs(node_arr, lidx, slidx, max_idx);
  return lowest_pair;
}

/* There is probably a more efficient way to construct this tree
 */
Node *build_tree(uint64_t *freq_arr) {
  uint16_t max_idx = get_num_chars(freq_arr);
  num_chars = max_idx;

  Node *fin_node;
  Node **node_arr = init_node_arr_from_chars(freq_arr, max_idx);

  // when n == 1, return node in array
  while (max_idx > 1) {
    // Find two lowest value nodes in node arr, w/ len number giving max len,
    // and their indicies
    Node **min_two = get_min_two(node_arr, max_idx);
    // Create node w/ the two lowest as children
    uint64_t count = min_two[0]->count + min_two[1]->count;

    Node *N = init_node(min_two[0], min_two[1], 0, count, 0);
    free(min_two);

    // remove the original two lowest value nodes, insert new node, decrease len
    // number we can remove the nodes since we can free the node mem in the
    // tree, not in the arr
    node_arr[max_idx - 2] = N;
    max_idx--;
    fin_node = N;
  }
  return fin_node;
}

/* given the main node N, the token (i.e. symbol), and
 * the code which defines the new node's position in
 * the tree.
 */
void reconstruct_tree(Node *N, uint8_t token, uint8_t code_len, uint64_t code) {
  Node *cur_node = N;
  for (unsigned int i = 64; i > 64 - code_len; i--) {
    bool is_leaf = i == (64 - code_len + 1);
    uint8_t leaf_token = is_leaf ? token : 0;
    uint64_t shift = (uint64_t)1 << (i - 1);
    if ((code & shift) == shift) {
      if (cur_node->r == NULL)
        cur_node->r = init_node(NULL, NULL, leaf_token, 0, is_leaf);
      cur_node = cur_node->r;
    } else {
      if (cur_node->l == NULL)
        cur_node->l = init_node(NULL, NULL, leaf_token, 0, is_leaf);
      cur_node = cur_node->l;
    }
  }
}

// Recursive call to get tree depth
// use cnt = 0 at top level
unsigned int _tree_depth(Node *N, unsigned int cnt) {
  if (N == NULL)
    return cnt;
  return fmax(_tree_depth(N->l, cnt + 1), _tree_depth(N->r, cnt + 1));
}

unsigned int tree_depth(Node *N) { return _tree_depth(N, 0) - 1; }

void _traverse(Node *N, CharCode *cur_cmprs, CharCode **write_table) {
  if (N->is_leaf) {
    cur_cmprs->token = N->token;
    write_table[N->token] = cur_cmprs;
    return;
  }

  CharCode *left_charcode = init_charcode(
      cur_cmprs->code | ((uint64_t)0 << (63 - cur_cmprs->code_len)),
      cur_cmprs->code_len + 1, 0);

  CharCode *right_charcode = init_charcode(
      cur_cmprs->code | ((uint64_t)1 << (63 - cur_cmprs->code_len)),
      cur_cmprs->code_len + 1, 0);

  free(cur_cmprs);

  _traverse(N->l, left_charcode, write_table);
  _traverse(N->r, right_charcode, write_table);
}

CharCode **traverse_tree(Node *N) {
  char *s = "failed to allocate CharCode array in traverse_tree\n";
  CharCode **ccarr =
      (CharCode **)safecalloc(TOKEN_SET_LEN, sizeof(CharCode *), s);
  CharCode *first_charcode = init_charcode(0, 0, 0);
  _traverse(N, first_charcode, ccarr);
  return ccarr;
}

void free_charcodes(CharCode **C) {
  for (int i = 0; i < TOKEN_SET_LEN; i++) {
    if (C[i])
      free(C[i]);
  }
}

bool trees_equal(Node *N1, Node *N2) {
  if ((N1 == NULL) && (N2 == NULL))
    return 1;
  else if ((N1 == NULL) != (N2 == NULL))
    return 0;
  else if (N1->is_leaf && N2->is_leaf)
    return N1->token == N2->token;
  return trees_equal(N1->l, N2->l) && trees_equal(N1->r, N2->r);
}

// i love recursion
void free_tree(Node *N) {
  if (N == NULL)
    return;
  free_tree(N->r);
  free_tree(N->l);
  free(N);
}

void write_charcode(FILE *outfile, CharCode *c) {
  /* write to file <1:char><1:num bits in tree code><x:tree code>
   * to current position in outfile
   */
  fwrite(&c->token, 1, 1, outfile);
  fwrite(&c->code_len, 1, 1, outfile);
  uint8_t num_code_bytes = NUM_BYTES(c->code_len);
  uint64_t code = c->code >> (64 - c->code_len);
  fwrite(&code, num_code_bytes, 1, outfile);
}

void encode(FILE *infile, FILE *outfile, CharCode **write_table) {
  // FILE FORMAT
  //  HEADER - tells decoder how to read file
  //    consists of
  //    <2 bytes: N = number of unique symbols in compressed file>
  //    <N symbols: <SYMBOL>> where
  //        SYMBOL = <symbol (1 byte) : \
  //              # depth of symbol in tree (1 byte) : \
  //              code (# of bits, plus padding to make it bytes)>
  //  CONTENTS - encoded symbols in file
  //  TAIL
  //    <1 byte: number of bits padding end of file>
  //
  // MSB of byte is the first instruction (i.e. left/right instruction)
  // Write to the file in chunks of 8 kb (8 bytes per u64, 1024 of)
  // write_chunk is the chunk of mem that gets written each time.
  // chunk_idx is the index of the write_chunk array that is being written to.
  // int_idx is the index of the uint64_t (given by write_chunk[chunk_idx]) that
  // hasn't been written to yet.
  //
  //  e.g. if the write_array has length 4, chunk_idx = 1 and int_idx = 2 is
  //
  // |       u64       |     u64         |       u64       |       u64       |
  // | . . . . . . . . | . . . . . . . . | . . . . . . . . | . . . . . . . . |
  //                         ^
  //                         | int_idx = 2
  //
  //                   ^^^^^^^^^^^^^^^^^^^
  //                      chunk_idx = 1
  //
  // The for(;;) loop below simply handles this data format, and writes it to
  // the output file. Since each encoded character is represented by a number
  // of bits that is not necessarily a multiple of 8, we have to do a lot of
  // bit shifting to get everyting stacked together nicely. The algorithm is
  // quite simple, though (ignoring some details):
  //
  // 1. Read in the next byte from the input file.
  // 2. Find the byte's corresponding CharCode C in the write_table
  // 3. Write C.code to write_chunk[chunk_idx] at position ind_idx
  //    by shifting C.code into position
  //    3.a If ind_idx + C.code_len > 64, then we write
  //        int_idx + C.code_len - 64 of the last bits of C.code to
  //        the next chunk_idx
  // 4. If chunk_idx == WRITE_CHUNK_LEN - 1, we write the write_chunk to the
  //    output file, set write_chunk to 0s, and set chunk_idx = int_idx = 0.
  //    Hop back to 1
  // 5. Once there are no more bytes to read, we write the last write chunks
  //    and the number of ragged bits at the end which we can ignore while
  //    decoding.
  //
  // Really, the for(;;) loop is a state machine. I should probably make a
  // diagram for it.
  fseek(infile, 0L, SEEK_END);
  uint64_t flen = ftell(infile);
  fseek(infile, 0L, SEEK_SET);

  char *s = "failed initializing write_chunk in encode\n";
  uint64_t *write_chunk =
      (uint64_t *)safecalloc(WRITE_CHUNK_SIZE, sizeof(uint64_t), s);
  uint64_t chunk_idx = 0;
  int8_t int_idx = 0;

  // start of file writing
  // first, write the number of chars (from global scope)
  fwrite(&num_chars, sizeof(uint16_t), 1, outfile);

  // we only need the code and code_len, and seperating
  // these from the CharCode object allows generalization in writing
  // accross write_chunk elements. I.e., each time we add to a write_chunk,
  // we add the most significant code_len bits of code.
  for (int i = 0; i < 256; i++)
    if (write_table[i])
      write_charcode(outfile, write_table[i]);

  // Start by loading a char from the infile
  uint8_t c = 0;
  uint64_t infile_pos = 1;
  fread(&c, 1, 1, infile);

  s = "failed initializing char arr in char freqs\n";
  uint8_t *read_chunk = safecalloc(READ_CHUNK_SIZE, 1, s);

  size_t read_idx = 0;
  fread(read_chunk, 1, READ_CHUNK_SIZE, infile);

  uint64_t code = write_table[c]->code;
  uint64_t code_len = write_table[c]->code_len;

  for (;;) {
    write_chunk[chunk_idx] |= code >> int_idx;
    int_idx += code_len;

    if (int_idx >= 64) {
      code = code << (code_len - int_idx - 64);
      code_len = int_idx - 64;
      chunk_idx++;
      int_idx = 0;
    } else { // load another char
      if (infile_pos == flen) {
        // if we are out of chars and here, we write and are finished!
        // set bytes to big endian order
        for (unsigned int i = 0; i < chunk_idx + 1; i++)
          write_chunk[i] = htonll(write_chunk[i]);

        // doing simple math to reduce number of redundant bits to less than 8
        uint8_t final_u64_num_junk_bits = 64 - int_idx;
        uint8_t full_junk_bytes = NUM_BYTES(final_u64_num_junk_bits) - 1;
        uint8_t num_bytes_to_write = 8 - full_junk_bytes;
        uint8_t tail_padding_zeros =
            final_u64_num_junk_bits - 8 * full_junk_bytes;
        uint64_t tail_chunk = write_chunk[chunk_idx];

        fwrite(write_chunk, sizeof(uint64_t), chunk_idx, outfile);
        fwrite(&tail_chunk, 1, num_bytes_to_write, outfile);
        fwrite(&tail_padding_zeros, sizeof(uint8_t), 1, outfile);
        break;
      }
      // load a char here
      if (read_idx == READ_CHUNK_SIZE) {
        read_idx = 0;
        fread(read_chunk, 1, READ_CHUNK_SIZE, infile);
      }
      c = read_chunk[read_idx];
      code = write_table[c]->code;
      code_len = write_table[c]->code_len;
      read_idx++;
      infile_pos++;
    }
    // write and reset write_chunk, set chunk_idx to 0
    if (chunk_idx == WRITE_CHUNK_SIZE) { // this chunk is full
      // set bytes to big endian order
      for (int i = 0; i < WRITE_CHUNK_SIZE; i++)
        write_chunk[i] = htonll(write_chunk[i]);

      fwrite(write_chunk, sizeof(uint64_t), WRITE_CHUNK_SIZE, outfile);
      memset(write_chunk, 0, WRITE_CHUNK_SIZE * sizeof(*write_chunk));
      chunk_idx = 0;
    }
  }
  free(write_chunk);
}

void decode(FILE *encoded_fh, FILE *decoded_fh) {
  // hop to end of file to get file len, padding zeros
  // offset of -1 so we can read the tail padding too.
  // once we are done, return it to start of file.
  uint8_t tail_padding_zeros = 0;
  fseek(encoded_fh, -1L, SEEK_END);
  uint64_t end_pos = ftell(encoded_fh) + 1;
  fread(&tail_padding_zeros, 1, 1, encoded_fh);
  fseek(encoded_fh, 0L, SEEK_SET);

  // read header
  uint16_t num_symbols;
  fread(&num_symbols, 2, 1, encoded_fh);

  // read symbols
  uint8_t token, code_len;
  uint64_t code;
  Node *root = init_node(NULL, NULL, 0, 0, 0);
  for (uint16_t i = 0; i < num_symbols; i++) {
    // symbol token
    fread(&token, 1, 1, encoded_fh);
    // # bits
    fread(&code_len, 1, 1, encoded_fh);
    // code
    uint8_t num_code_bytes = NUM_BYTES(code_len);

    code = 0;
    fread(&code, num_code_bytes, 1, encoded_fh);

    code <<= (64 - code_len);

    reconstruct_tree(root, token, code_len, code);
  }

  // read actual file data
  uint8_t byte;
  uint8_t byte_valid_bits;
  char *s = "failed creating write chunk\n";
  uint8_t *write_chunk = safecalloc(WRITE_CHUNK_SIZE, 1, s);
  size_t write_idx = 0;

  Node *N = root;
  uint64_t cur_file_pos = ftell(encoded_fh);
  for (; cur_file_pos < end_pos - 1; cur_file_pos++) {
    // read byte
    fread(&byte, 1, 1, encoded_fh);
    // for each bit in byte, move in tree
    byte_valid_bits = cur_file_pos == end_pos - 2 ? tail_padding_zeros : 0;
    for (int i = 7; i >= byte_valid_bits; i--) {
      uint8_t shift = 1 << i;
      if ((byte & shift) == shift)
        N = N->r;
      else
        N = N->l;

      if (N->is_leaf) {
        write_chunk[write_idx] = N->token;
        write_idx++;
        if (write_idx == WRITE_CHUNK_SIZE) {
          fwrite(write_chunk, 1, write_idx, decoded_fh);
          write_idx = 0;
        }
        N = root;
      }
    }
  }
  fwrite(write_chunk, 1, write_idx, decoded_fh);

  free_tree(root);
}

void help(char *argv[]) {
  fprintf(stderr, "Usage: %s [-d] [-f output filename] [file...]\n", argv[0]);
  fprintf(stderr, "  -d decode\n");
  fprintf(stderr, "  -f output file name\n");
}

int main(int argc, char *argv[]) {
  bool encode_file = 1;
  char **outfile_name = NULL;
  char *fin = NULL;

  int opt;
  while (optind < argc) {
    if ((opt = getopt(argc, argv, "df:")) != -1) {
      switch (opt) {
      case 'd':
        encode_file = 0;
        break;
      case 'f':
        outfile_name = &optarg;
        break;
      case '?':
        if (optopt == 'f') {
          fprintf(stderr, "Option -f requires an argument.\n");
          exit(1);
        }
      default:
        help(argv);
        exit(1);
      }
    } else {
      fin = argv[optind];
      optind++;
    }
  }

  if (fin == NULL) {
    help(argv);
    exit(1);
  }

  FILE *infile;
  infile = fopen(fin, "r");
  if (!infile) {
    fprintf(stderr, "failed to open %s\n", fin);
    exit(1);
  }

  if (encode_file) {
    char *encoded_file = strcat(fin, ".pine");

    FILE *outfile;
    outfile = fopen(encoded_file, "w");
    if (!outfile) {
      fprintf(stderr, "failed to open %s\n", encoded_file);
      exit(1);
    }

    uint64_t *freq_arr = calculate_char_freqs(infile);
    Node *tree = build_tree(freq_arr);
    CharCode **C = traverse_tree(tree);

    encode(infile, outfile, C);

    free_charcodes(C);
    free_tree(tree);
    free(freq_arr);

    fclose(outfile);
  } else {
    if (outfile_name == NULL) {
      outfile_name = &fin;
      if (strstr(*outfile_name, ".pine") != NULL)
        (*outfile_name)[strlen(*outfile_name) - 5] = '\0';
      else
        *outfile_name = strcat("decoded_", fin);
    }

    FILE *outfile;
    outfile = fopen(*outfile_name, "w");
    if (!outfile) {
      fprintf(stderr, "failed to open %s\n", *outfile_name);
      exit(1);
    }

    decode(infile, outfile);
    fclose(outfile);
  }
  fclose(infile);
}
