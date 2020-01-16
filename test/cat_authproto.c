#include <stdlib.h>

#include "../helpers/authproto.h"

int main() {
  int eof_permitted = 0;
  for (;;) {
    char type;
    char *message;
    type = ReadPacket(0, &message, eof_permitted);
    if (type == 0) {
      return 0;
    }
    WritePacket(1, type, message);
    free(message);
    eof_permitted = !eof_permitted;
  }
}
