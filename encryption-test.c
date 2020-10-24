#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define DEBUG 0

//#define FILENAME "../2_data/random_8KiB.dat"
// #define FILENAME "../2_data/random_64KiB.dat"
// #define FILENAME "../2_data/random_1MiB.dat"
//#define BUFFERSIZE 1024*64

int crypto_aead_encrypt(
  unsigned char *c, unsigned long long *clen,
  const unsigned char *m, unsigned long long mlen,
  const unsigned char *ad, unsigned long long adlen,
  const unsigned char *nsec,
  const unsigned char *npub,
  const unsigned char *k
);

int crypto_aead_decrypt(
  unsigned char *m, unsigned long long *mlen,
  unsigned char *nsec,
  const unsigned char *c, unsigned long long clen,
  const unsigned char *ad, unsigned long long adlen,
  const unsigned char *npub,
  const unsigned char *k
);


int main(int argc, char *argv[]) {

  /* declare variables */
  //unsigned char *plaintext;
  //unsigned long long plaintext_length;
  unsigned char *plaintext_bis;
  unsigned long long *plaintext_length_bis;

  unsigned char *authenticated_data;
  unsigned long long authenticated_data_length;

  unsigned char *ciphertext;
  unsigned long long *ciphertext_length;

  unsigned char *nsec;
  unsigned char *npub;
  unsigned char *key;

  if (argc != 3) {
    printf("Usage: %s [BufferSize] [Filechoice]\n", argv[0]);
    return -1;
  }
  unsigned long long BUFFERSIZE = atoi(argv[1]) * 1024;
  //  unsigned char *buffer = malloc(BUFFERSIZE);// Eerste argument
  char FILENAME[100];

  int choice = atoi(argv[2]);
  switch(choice)
  {
    case 1:
      strcpy(FILENAME, "../2_data/random_8KiB.dat");
      break;
    case 2:
      strcpy(FILENAME, "../2_data/random_64KiB.dat");
      break;
    case 3:
      strcpy(FILENAME, "../2_data/random_1MiB.dat");
      break;
    default:
      printf("ERROR: NO FILEPATH GIVEN!\n");
      return -2;
      break;
  };

  if (DEBUG)
    printf("Running with %llu bytes on file\n", BUFFERSIZE);
  unsigned char buffer[64*1024]; //[BUFFERSIZE];
  
  int rv, i;
  FILE * fh;

  /* allocate memory */
  key = (unsigned char *)malloc(16*sizeof(unsigned char));
  ciphertext = (unsigned char *)malloc(BUFFERSIZE*sizeof(unsigned char));
  ciphertext_length = (unsigned long long *)malloc(1*sizeof(unsigned long long));;
  plaintext_bis = (unsigned char *)malloc(BUFFERSIZE*sizeof(unsigned char));
  plaintext_length_bis = (unsigned long long *)malloc(1*sizeof(unsigned long long));;
  nsec = (unsigned char *)malloc(1*sizeof(unsigned char));
  npub = (unsigned char *)malloc(1*sizeof(unsigned char));
  
  /*if (key & ciphertext & ciphertext_length & plaintext_bis & plaintext_length_bis & nsec & npub)
  {
	  return -1;
  }*/
  /* assigning memory */
  *nsec = 01;
  *npub = 15;


  /* set the associated data */
  authenticated_data = (unsigned char*)"hello world";
  authenticated_data_length = (unsigned long long)(11);


  /* set the key */
  for(i=0;i<16;i++) {
    key[i] = 65 + i;
  }


  /* open the file */
  fh = fopen(FILENAME, "r");
  if(!fh) {
    printf("ERROR: Unable to open %s\n", FILENAME);
    return(1);
  }

/*  printf("key: \t\t\t%s\n", key);
  printf("\n");
*/

  // STARTING MEASUREMENT //
  struct timeval start;
  gettimeofday(&start, NULL);

  /* read data from file */
  rv = fread(buffer, /*sizeof(buffer)*/BUFFERSIZE, 1, fh); /* Read BUFFERSIZE bytes to buffer*/
  while(rv) {
/*    printf("block counter: %d\n", blockcounter++);
    printf("\tplaintext: \t\t\t\t" );
    for(i=0;i<10;i++)
      printf("0x%02X ", buffer[i]);
    printf("...\n");
*/

    /* Encrypt the ciphertext */
    rv = crypto_aead_encrypt(ciphertext, ciphertext_length,
      &buffer[0], 1024,
      authenticated_data, authenticated_data_length,
      nsec, npub, key);

/*    printf("\tciphertext (ENCRYPTED): \t\t" );
    for(i=0;i<10;i++)
      printf("0x%02X ", ciphertext[i]);

    printf("...\n");
*/

    /* Decrypt the ciphertext */
    rv = crypto_aead_decrypt(plaintext_bis, plaintext_length_bis,
      nsec,
      ciphertext, *ciphertext_length,
      authenticated_data, authenticated_data_length,
      npub, key);

/*    printf("\tplaintext_bis (DECRYPTED): \t\t" );
    for(i=0;i<10;i++)
      printf("0x%02X ", plaintext_bis[i]);
    printf("...\n");
*/
    /* read new data from file */
    rv = fread(buffer, /*sizeof(buffer)*/BUFFERSIZE, 1, fh); // read 10 bytes to our buffer
  }


  // time measurement //
  struct timeval end;
  gettimeofday(&end, NULL);
  
  unsigned long dtime = ((end.tv_sec-start.tv_sec)*1000000) + (end.tv_usec-start.tv_usec);
  if (DEBUG)  printf("TOTAL TIME: %lu microseconds -> %f seconds\n", dtime, (float)dtime/1000000);
  printf("%lu",dtime);
  if (DEBUG)  printf("%f Bytes/second or %f KiB/second.\n", BUFFERSIZE*1024/((float)dtime/1E6), BUFFERSIZE/((float)dtime/1E6));

  //free(buffer);
  free(key);
  free(ciphertext);
  //free(ciphertext_length);
  free(plaintext_bis);
  free(plaintext_length_bis);
  free(nsec);
  free(npub);
  
  /* close the file */
  fclose(fh);

  return 0;
}
