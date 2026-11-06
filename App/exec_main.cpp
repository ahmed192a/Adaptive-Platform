/**
 * @file exec_main.cpp
 * @brief EM
 * @version 0.1
 * @date 2022-06-28
 * 
 * @copyright Copyright (c) 2022
 * 
 */
/***************************************************************************
 *                          Include Section                                *
 ***************************************************************************/
#include <iostream> // std::cout std::cin
#include <vector>   // std::vector
#include <map>      // std::map
#include <string>   // std::string
#include <sys/stat.h>   // mkdir
#include <dirent.h>    // opendir
#include <filesystem> // to get all files in certain dir
#include "ara/exec/parser/Parser.h"
#include "errno.h"      // errno
#include "unistd.h"
#include <fcntl.h>
#include <utility>
#include <sys/wait.h>
#include <poll.h>
#include "ara/exec/execution_error_wire.h"
#include <filesystem>

#include "ara/exec/function_group.h"
#include "ara/exec/function_group_state.h"
#include "ara/Log_trace/Log.h"

/***************************************************************************
 *                           Define Section                                *
 ***************************************************************************/
#define MC_MF   "/Manifests/machine_manifest.json"
#define EM_MF "/Manifests/execution_manifest.json"
#define SM_FIFO "processes/sm_process/state_client_fifo"
#define EM_ERR_FIFO "processes/sm_process/execution_error_fifo" // read by ara::exec::StateClient (SWS_EM_01309)
#define MAX_BUF 1024

/***************************************************************************
 *                      Namespace Section                                 *
 ***************************************************************************/
using namespace ara::exec;
using namespace ara::exec::parser;
using namespace std;

/***************************************************************************
 *                      Fucntions Decleration Section                      *
 ***************************************************************************/

int exec_init();
void change_state(std::string const&n_FG, std::string const&n_state);
void view_out();
void p_operate();
void p_terminate();
static int err_fd = -1;                  // write end of the execution-error FIFO (opened O_RDWR: never blocks)
void publish_execution_error(std::string const &fg, std::uint32_t code);
void monitor_children();
void handle_sigterm(int sig){
    cout<<"EM: SIGTERM received"<<endl;
    change_state("MachineFG", "Shuttingdown");
    cout<<"EM: Shutting down"<<endl;
    exit(0);
}
/***************************************************************************
 *                      Global Variable Section                            *
 ***************************************************************************/
static map<string, ara::exec::parser::GLOB, std::less<>> sys_FG;                // Map of all function groups of the system
std::vector<Process> process_pool;              // Vector of all processes of the system

// Backup streambuffers of  cout
streambuf* stream_buffer_cout = cout.rdbuf();   // backup cout stream buffer
streambuf* stream_buffer_cin = cin.rdbuf();     // backup cin stream buffer

/**
 * @brief Main program of Excution Managiment program
 * @param argc Number of arguments
 * @param argv Arguments
 * 
 * @note   this is the Main program of Excution managiment which has the following functions:
 *              1. Parsing manifest files
 *              2. start state management
 *              3. get the state from SM
 *              4. switch state (start terminating sequence and starting sequence)
 * @return  0 if success or 1 if failed 
 */
int main(int argc, char ** argv){
    signal(SIGTERM, handle_sigterm);    // handle SIGTERM signal used for terminating the system
    signal(SIGKILL, handle_sigterm);    // handle SIGKILL signal used for terminating the system
    std::cout << "EM: Hello, World!" << std::endl;

    // check if there are files the the directory "processes/redirected"
    // if there are files, then delete them
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("processes/redirected")) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            if (ent->d_name[0] != '.') {
                remove((string("processes/redirected/") + ent->d_name).c_str());
            }
        }
        closedir (dir);
    }
    std::cout << "EM: Directory processes/redirected is empty" << std::endl;    



    //Create redirected folder for cout of all processes,check if the folder exists
    // if(filesystem::exists("processes/redirected")) filesystem::remove_all("processes/redirected");
    // filesystem::create_directories("processes/redirected");    

    // file stream to write the output of EM to a EM.txt file
    fstream file;   
    file.open("processes/redirected/EM.txt", ios::out); 
    cout.rdbuf(file.rdbuf()); 
    // open the file EM.txt to visualize it in real time in another genome terminal
    //system("gnome-terminal -- sh -c 'tail -f -n 100 processes/redirected/EM.txt'");
    std::cout << "EM: EM.txt is created" << std::endl;



/***************************************************************************
 *                      Start Of Execution Manager                          *
 ***************************************************************************/
    // Initialize the system
    cout << "\n-    EM Initialization..."<<endl;
    // print the pid of EM
    cout << "-    EM: PID: " << getpid() << endl << endl;
    exec_init();                            // initialize the system and parse the manifest files

    mkfifo(SM_FIFO, 0666);                  // create the fifo to communicate with State Management process
    std::string msg;                        // message to send to SM
    msg.resize(MAX_BUF);                    // resize the message to MAX_BUF bytes

    //change the state of MachineFG to Starting-up to start the state management process
    // change_state("MachineFG" , "Pre-Starting-up"); 
    change_state("MachineFG" , "Starting-up"); 

    mkfifo(EM_ERR_FIFO, 0666);              // execution errors for the State Management StateClient
    err_fd = open(EM_ERR_FIFO, O_RDWR | O_NONBLOCK);

    int fd = open(SM_FIFO, O_RDONLY);   // open the fifo to read the state from SM

    /****************************** Main Logic of EM ***********************************/
    // get FG name and new state from SM an change the state
    // The FIFO is a byte stream: requests are newline-terminated ("<FG>/<state>\n") and may arrive
    // glued together or split, so they are re-assembled here.
    std::string pending;
    while(sys_FG["MachineFG"].current_FGS->get_states() != "off"){
        char buf[MAX_BUF];
        // Wake up regularly to notice processes that died without being asked to (SWS_EM_01309).
        monitor_children();
        struct pollfd pfd{fd, POLLIN, 0};
        int const pr = poll(&pfd, 1, 250);
        if (pr == 0) continue;
        if (pr < 0) { if (errno == EINTR) continue; }
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("EM: read");
            change_state("MachineFG", "off");
            continue;
        }
        if (n == 0) {                       // every writer closed the FIFO: wait for the next one
            close(fd);
            fd = open(SM_FIFO, O_RDONLY);
            if (fd < 0) { change_state("MachineFG", "off"); fd = -1; break; }
            continue;
        }
        pending.append(buf, static_cast<size_t>(n));
        size_t eol;
        while ((eol = pending.find('\n')) != std::string::npos &&
               sys_FG["MachineFG"].current_FGS->get_states() != "off") {
            std::string line = pending.substr(0, eol);
            pending.erase(0, eol + 1);
            if (line.empty()) continue;
            cout << "-    EM: New State from SM " << line.size() << " " << line << endl;
            size_t slash = line.find('/');
            if (slash == std::string::npos || sys_FG.find(line.substr(0, slash)) == sys_FG.end()) {
                cout << "-    EM: ignoring invalid request \"" << line << "\"" << endl;
                continue;
            }
            std::string FG_name = line.substr(0, slash);
            std::string new_state = line.substr(slash + 1);
            cout << "-    EM: FG: " << FG_name << " New State: " << new_state << endl;
            change_state(FG_name, new_state);
        }
    }
    /***********************************************************************************/

    if (fd >= 0) close(fd);             // close the fifo
    unlink(SM_FIFO);                    // delete the fifo
    if (err_fd >= 0) close(err_fd);
    unlink(EM_ERR_FIFO);
    cout << "\n-    EM Terminated."<<endl;

    cout.rdbuf(stream_buffer_cout);     // restore the cout stream buffer
    file.close();                       // close the file stream
    view_out();                         // view the output of all processe
    return 0;
}
/**
 * @brief Publish an execution error for a function group to the State Management client.
 * @note SWS_EM_01309/SWS_EM_02543: abnormal termination => execution error (default value 1).
 */
void publish_execution_error(std::string const &fg, std::uint32_t code)
{
    if (err_fd < 0) return;
    std::string const msg = ara::exec::FormatExecutionErrorLine(fg, code);
    ssize_t const r = write(err_fd, msg.data(), msg.size());
    (void)r;
}

/**
 * @brief Reap processes that terminated without an EM request and report them as execution errors.
 *        A process that exits with status 0 on its own is treated as a normal termination.
 */
void monitor_children()
{
    for (auto &p : process_pool) {
        if (!p.prun || p._pid <= 0) continue;
        int status = 0;
        if (waitpid(p._pid, &status, WNOHANG) != p._pid) continue;
        std::string fg = "MachineFG";
        if (p.current_config && !p.current_config->machine_instance_refs.empty())
            fg = p.current_config->machine_instance_refs.front().function_group;
        bool const clean = !ara::exec::IsAbnormalTermination(status);
        if (clean)
            cout << "-    EM: process " << p.name << " exited on its own (status 0)" << endl;
        else
            cout << "-    EM: ERROR: process " << p.name << " terminated unexpectedly (status " << status
                 << ") in function group " << fg << endl;
        close(p.exec_clinet_fd);
        unlink(("processes/" + p.name + "/execution_client_fifo").c_str());
        p.prun = false;
        p.current_config = nullptr;
        p._pid = 0;
        if (!clean) publish_execution_error(fg, ara::exec::kDefaultExecutionError);
    }
}

/**
 * @brief initialize the system and parse the manifest files
 * 
 * @return void
 */
int exec_init(){
    
    Parser MachineManifestParser(std::string(get_current_dir_name())+ MC_MF);
    Parser ExecutionManifestParser(std::string(get_current_dir_name())+ EM_MF);
    auto MM = MachineManifestParser.ParseMachineManifest(sys_FG);
    if (MM == nullptr){
        std::cerr << "Machine Manifest isn't Valid" <<std::endl;
        exit(1); 
    }
    cout<<"- Machine Manifest ID: \""+MM->manifest_id+"\" Parsed Successfully, FGs No.: "<<sys_FG.size()<<endl;

    auto EM = ExecutionManifestParser.ParseExecutionManifest();
    if (EM == nullptr){
        std::cerr << "Execution Manifest isn't Valid" <<std::endl;
        exit(1); 
    }
    cout<<"- Execution Manifest ID: \""+EM->manifest_id+"\" Parsed Successfully, Processes No.: "<<EM->processes.size()<<endl;
    process_pool = EM->processes; 
   
    return 0;
}
/**
 * @brief operate is the method responsible for starting the processes that is satisfied by the FG state
 *          after changing the state of the FG 
 * 
 * @return void
 */
void p_operator(){
    for(auto &process : process_pool){     
        if(process.prun|| process._pid != 0)continue;   // if the process is running continue
        bool will_run = false;                          // flag to indicate if the process will run or not
        //check if the current state doesnt violate the configuration,otherwise terminate
        for(int config = 0; config < process.startup_configs.size();config++){  
            bool violate = false;           // flag to indicate if the current state violates the configuration
            // for each machine instance(FG) reference in the current configuration
            for(auto &mref: process.startup_configs[config].machine_instance_refs){  
                //Get the current state of the FG
                string fg_statue = sys_FG[mref.function_group].current_FGS->get_states(); 
                //Process wont run if fg_state is not process_config_states
                if (!count(mref.modes.begin(), mref.modes.end(), fg_statue)){
                    violate = true;         
                    break;
                }
            }
            if(!violate){           // if the current state doesnt violate the configuration
                process.current_config = &process.startup_configs[config];  // set the current configuration 
                will_run = true;    // set the flag to true to indicate that the process will run
                break;
            }
        }
        if(will_run)process.start();    // if the process will run start it
    }
}
/**
 * @brief terminate is the method responsible for terminating the processes that is not satisfied by the FG state 
 *            after changing the state of the FG 
 * 
 * @return void
 */
void p_terminator(){
    // loop on process_pool from back to front
    for(int i = process_pool.size()-1; i >= 0; i--){
        // for(auto &process : process_pool){
        // if the process is not running continue
        if(!process_pool[i].prun)continue;
        std::cout << "-    EM: Terminating Process: " << process_pool[i].name << std::endl;
        // for each machine instance(FG) reference in the current configuration
        for(auto &mref: process_pool[i].current_config->machine_instance_refs){
            //Get the current state of the FG
            string fg_statue = sys_FG[mref.function_group].current_FGS->get_states(); 
            //Process wont terminate if fg_state is not process_config_states
            if (!count(mref.modes.begin(), mref.modes.end(), fg_statue)){
                std::cout << "-    EM: Terminating Process: 22 " << process_pool[i].name << std::endl;
                process_pool[i].terminate();
                break;
            }
        }
        }
    // }


}
/**
 * @brief change_state Change the state of a function group and terminate the process if it's 
 *          not in vaild state and start the process if it's in valid state
 * @param n_FG          The function group name
 * @param n_state       The new state
 *
 * @return void 
 */
void change_state(std::string const&n_FG, std::string const&n_state)
{
    cout<<"---------------------------------------------------------------"<<endl;
    cout<<"- EM: State Transition ("<<sys_FG[n_FG].current_FGS->get_FGname()<<"):  "+sys_FG[n_FG].current_FGS->get_states()+" --> "<<n_state<<endl;
    cout<<"---------------------------------------------------------------"<<endl;
    sys_FG[n_FG].current_FGS.reset();
    FunctionGroupState::CtorToken token={n_FG,n_state};
    sys_FG[n_FG].current_FGS = std::make_shared<FunctionGroupState>(std::move(token));
    cout<<"\tTermination Log:"<<endl<<endl;
    p_terminator();
    cout<<"---------------------------------------------------------------"<<endl;
    cout<<"\tOperation Log:"<<endl<<endl;
    p_operator();
    cout<<"---------------------------------------------------------------"<<endl;
}
/**
 * @brief view_out Print the output files in the redirected folder
 *
 * @return void 
 */
void view_out(){
    std::string root = std::string(get_current_dir_name())+"/processes/redirected/", ext = ".txt";
    if (filesystem::exists(root) && filesystem::is_directory(root))
    {
        for (auto const & entry : filesystem::recursive_directory_iterator(root))
        {
            if (filesystem::is_regular_file(entry) && entry.path().extension() == ext){
                cout<<entry.path().filename().c_str()<<endl;
                system(("cd processes/redirected;cat "+ std::string(entry.path().filename().c_str())+";").c_str());
            }
        }
    }
}