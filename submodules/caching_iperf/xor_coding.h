
#define XOR_CODING_VECTOR_TYPE (uint64_t) // todo?
#define XOR_CODING_VECTOR_SIZE 32

typedef struct XorCodingConfig_s
{
  uint32_t payloadSizeBytes;
} XorCodingConfig_t;

typedef struct CodedPacket_s
{
  uint64_t vector; // 8 bytes, 64 packets // todo maybe this doesnt need tobe an array
  uint8_t payload[]; // Payload size will be a chunk long
} CodedPacket_t;

void XorCoding_DoCoding(XorCodingConfig_t *config, CodedPacket_t *src, CodedPacket_t *dst);
void XorCoding_PrintEntry(XorCodingConfig_t *config, CodedPacket_t *c);
