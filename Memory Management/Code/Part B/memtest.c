#include "types.h"
#include "stat.h"
#include "user.h"
//A function to introduce delay in the program so as to increase the
//number of context switches

int main()
{
    // int pids[10];
    int count=0;
    for(int i=0;i<10;i++){
        int pid=fork();
        if(pid==0){
        	
            for(int j=0;j<20;j++){
                int *ptr=malloc(4096);
                for(int k=0;k<1024;k++){
                    ptr[k]=k*k;
                }
                for(int k=0;k<1024;k++){
                    if(ptr[k]!=k*k) count++;
                    
                }
            }
            exit();
        }
    }
    
    if(!count) printf(1,"All Tests Passed\n");
    else printf(1,"Failed\n");
    for(int i=0;i<10;i++){
        wait();
    }
    exit();
}
