#include <stdbool.h>

typedef enum 
{
  RED_CONTENT,
  GRN_CONTENT,
  BLU_CONTENT,
  NUM_CONTENT_TYPES,
} ContentTypes_e;

#define RED_PREFIX "/red"
#define BLU_PREFIX "/blu"
#define GRN_PREFIX "/grn"

// API
void CCN_NC_Init(void);
void CCN_NC_ShowCS(void);
void CCN_NC_Produce(ContentTypes_e t, bool overwrite);
void CCN_NC_Interest(char *prefixStr);

// Shell cmds
int cmd_ccnl_nc_produce(int argc, char **argv);
int cmd_ccnl_nc_interest(int argc, char **argv);
int cmd_ccnl_nc_show_cs(int argc, char **argv);
int cmd_ccnl_nc_show_pit(int argc, char **argv);
int cmd_ccnl_nc_rm_cs(int argc, char **argv);
int cmd_ccnl_nc_rm_cs_all(int argc, char **argv);
int cmd_ccnl_nc_set_hw(int argc, char **argv);

