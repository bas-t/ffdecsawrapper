
int find_pid_from_socket(char *origstr);
int send_evocamd(int cardnum, unsigned short servid, unsigned short msg);
bool init_evocamd(int cardnum, char *scDir);
int start_loopbackca(int cardnum);
bool shutdown_evocamd(int cardnum);

