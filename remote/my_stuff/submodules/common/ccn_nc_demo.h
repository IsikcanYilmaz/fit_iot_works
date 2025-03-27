
typedef enum 
{
  RED_CONTENT,
  BLU_CONTENT,
  GRN_CONTENT,
  NUM_CONTENT_TYPES,
} ContentTypes_e;

#define RED_PREFIX "/red"
#define BLU_PREFIX "/blu"
#define GRN_PREFIX "/grn"

int cmd_ccnl_nc_produce(int argc, char **argv);
int cmd_ccnl_nc_interest(int argc, char **argv);
int cmd_ccnl_nc_show_cs(int argc, char **argv);
int cmd_ccnl_nc_rm_cs(int argc, char **argv);


