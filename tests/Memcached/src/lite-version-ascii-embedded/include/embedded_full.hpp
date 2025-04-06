typedef struct lite_item {
    uint8_t nkey;
    uint8_t *key;
    uint8_t flags[2];
    uint8_t *value;
    uint32_t nbytes;
} lite_item;
