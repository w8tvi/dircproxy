#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#else /* HAVE_CRYPT_H */
#include <unistd.h>
#endif /* HAVE_CRYPT_H */

static void _saltchar(char *);

int main() {
  char pass[80], salt[3], *ret;
  
  printf("Enter a password to encrypt: ");
  ret = fgets(pass, sizeof(pass), stdin);
  if (ret) {
    char *ptr, *enc;

    ptr = pass + strlen(pass);
    while ((ptr >= ret) && (*ptr <= 32)) *(ptr--) = 0;

    srand(time(0));
    _saltchar(&(salt[0]));
    _saltchar(&(salt[1]));
    salt[2] = 0;

    enc = crypt(pass, salt);
    printf("\nPassword encrypted is: %s\n", enc);
    return 0;
  } else {
    printf("\nNo password received.\n");
    return 1;
  }
}

static void _saltchar(char *c) {
  static char *chars = "abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "01234567890";
  int ran;

  ran = (int)((double)(strlen(chars) - 1) * (rand() / (RAND_MAX + 1.0)));
  *c = chars[ran];
}
