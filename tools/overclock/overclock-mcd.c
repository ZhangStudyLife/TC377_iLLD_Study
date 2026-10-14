#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "mcd_api.h"
#define LOAD(fn) __typeof__(&fn) p_##fn = (__typeof__(&fn))(uintptr_t)GetProcAddress(dll, #fn)
#define CALL(fn, ...) do { printf("%s...\n", #fn); mcd_return_et r=p_##fn(__VA_ARGS__); printf("result=%u\n",(unsigned)r); if(r) { mcd_error_info_st e={0}; p_mcd_qry_error_info_f(NULL,&e); printf("error=%u %s\n",(unsigned)e.error_code,e.error_str); return 1; } } while(0)
int main(int argc, char **argv)
{
    setbuf(stdout, NULL); printf("argc=%d first=%s\n",argc,argc>1?argv[1]:"none");
    HMODULE dll=LoadLibraryA("C:\\Program Files\\DAS64\\clients\\mcdxdas.dll");
    if(!dll) { printf("DLL error=%lu\n",GetLastError()); return 1; }
    LOAD(mcd_initialize_f); LOAD(mcd_open_server_f); LOAD(mcd_qry_systems_f); LOAD(mcd_qry_servers_f);
    LOAD(mcd_qry_devices_f); LOAD(mcd_qry_cores_f); LOAD(mcd_open_core_f);
    LOAD(mcd_qry_error_info_f); LOAD(mcd_qry_state_f); LOAD(mcd_close_core_f);
    LOAD(mcd_close_server_f); LOAD(mcd_exit_f);
    LOAD(mcd_run_f); LOAD(mcd_qry_mem_spaces_f); LOAD(mcd_execute_txlist_f);
    mcd_api_version_st version={.v_api_major=1,.v_api_minor=1,.author="SPRINT Release"};
    mcd_impl_version_info_st impl={0};
    CALL(mcd_initialize_f,&version,&impl);
    printf("vendor=%s version=%u.%u.%u\n",impl.vendor,impl.v_imp_major,impl.v_imp_minor,impl.v_imp_build);
    mcd_server_info_st servers[8]={0};
    uint32_t ns=8;
    CALL(mcd_qry_servers_f,"localhost",1,0,&ns,servers);
    for(uint32_t i=0;i<ns;i++) printf("server %u=%s hw=%s instance=%s\n",i,servers[i].server,servers[i].acc_hw,servers[i].system_instance);
    mcd_server_st *server=NULL;
    CALL(mcd_open_server_f,"","McdServerName=\"UDAS\" McdAccHw=\"DAS JDS TriBoard TC3XX V2.0 TBAXFHYE\"",&server);
    uint32_t n=8;
    mcd_core_con_info_st systems[8]={0},devices[8]={0},cores[8]={0};
    CALL(mcd_qry_systems_f,0,&n,systems);
    printf("systems=%u %s\n",n,systems[0].system);
    n=8; CALL(mcd_qry_devices_f,&systems[0],0,&n,devices);
    printf("devices=%u %s\n",n,devices[0].device);
    n=8; CALL(mcd_qry_cores_f,&devices[0],0,&n,cores);
    for(uint32_t i=0;i<n;i++) printf("core %u=%s\n",i,cores[i].core);
    mcd_core_st *core=NULL;
    CALL(mcd_open_core_f,&cores[0],&core);
    mcd_core_state_st state={0};
    CALL(mcd_qry_state_f,core,&state);
    printf("core state structure read successfully (%zu bytes)\n",sizeof(state));
    printf("state=%u\n",(unsigned)state.state);
    mcd_memspace_st spaces[16]={0};
    n=16; CALL(mcd_qry_mem_spaces_f,core,0,&n,spaces);
    for(uint32_t i=0;i<n;i++) printf("space %u=%s min=%llx max=%llx\n",spaces[i].mem_space_id,spaces[i].mem_space_name,(unsigned long long)spaces[i].min_addr,(unsigned long long)spaces[i].max_addr);
    if(argc>1 && strcmp(argv[1],"run")==0) { CALL(mcd_run_f,core,1); } for(int i=(argc>1 && (strcmp(argv[1],"run")==0 || strcmp(argv[1],"half")==0))?2:1;i<argc;i++) {
        uint32_t value=0;
        mcd_tx_st tx={0};
        tx.addr.mem_space_id=spaces[0].mem_space_id;
        tx.addr.address=strtoull(argv[i],NULL,16);
        char *assign=strchr(argv[i],'=');
        if(assign) value=strtoul(assign+1,NULL,16);
        tx.access_type=assign?MCD_TX_AT_W:MCD_TX_AT_R;
        tx.access_width=(argc>1 && strcmp(argv[1],"half")==0)?2:4;
        tx.data=(uint8_t *)&value;
        tx.num_bytes=tx.access_width;
        mcd_txlist_st list={.tx=&tx,.num_tx=1};
        CALL(mcd_execute_txlist_f,core,&list);
        float f; memcpy(&f,&value,4);
        printf("address=%llx u32=%08x float=%g bytes_ok=%u\n",(unsigned long long)tx.addr.address,value,f,tx.num_bytes_ok);
    }
    CALL(mcd_close_core_f,core);
    CALL(mcd_close_server_f,server);
    p_mcd_exit_f();
    return 0;
}
