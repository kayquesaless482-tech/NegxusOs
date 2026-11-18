/* Runtime dos aplicativos .nxe: recebe a tabela de API do kernel e chama app_main */
#include "nxe.h"
const nx_api *A;
extern int app_main(const nx_api *api, const char *arg);
int nxe_main(const nx_api *api, const char *arg) { A = api; return app_main(api, arg); }
