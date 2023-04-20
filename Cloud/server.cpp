/**
 * @file server.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief 
 * @version 0.1
 * @date 2022-07-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <unistd.h>// close socket
#include <sys/socket.h>//socket
#include <arpa/inet.h> //inet_addr
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include "./metadata.h"
// include header for DIR
#include <dirent.h>
#include <map>

#define PORT 8888
#define PACKAGE_FILE "flash_tiva.out"
#define METADATA_FILE "metadata.dat"
#define PAKAGES_DIR "packages"
#define BUFFER_SIZE 100000



using namespace std;
std::vector<char> ReadAllBytes(char const *filename)
{
    ifstream ifs(filename, ios::binary | ios::ate);
    ifstream::pos_type pos = ifs.tellg();
    std::vector<char> result(pos);
    ifs.seekg(0, ios::beg);
    ifs.read(&result[0], pos);
    return result;
}

std::vector<std::string> findMetadataDat(const std::filesystem::path& root)
{
    std::vector<std::string> paths;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file() && entry.path().filename() == "metadata.dat")
        {
            paths.push_back(entry.path().string());
        }
    }

    return paths;
}


std::map<std::string, std::string, std::less<>> metadata_parser()
{
    // get all metadata.dat files from subfolders of packages folder
    std::string path = PAKAGES_DIR;
    std::vector<std::string> files = findMetadataDat(path);
    
    // read each file and parse the metadata into vector<MetaData>
    map<string, string, std::less<>> metadata_map;
    for(auto const& file:files){
        //read the string from the file
        std::ifstream file_stream(file);

        std::string str((std::istreambuf_iterator<char>(file_stream)),
                 std::istreambuf_iterator<char>());
        // parse the string into MetaData
        MetaData md(str);
        metadata_map[md.serializeToJson()] = file;

    }

    return metadata_map;
}


/**
 * @brief main
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char const *argv[])
{

    cout << endl;
    cout<<"\tcloud Intialization ..."<<endl;
    cout <<"\tcloud pid : " << getpid()<< endl<<endl;
    cout<<"------------------------------------------------------"<<endl;
   
	int server_fd;
    int new_socket;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	
	// Creating socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		cout<<"\t[CLOUD] open socket failed"<<endl;
		exit(EXIT_FAILURE);
	}
    cout<<"[CLOUD] socket created"<<endl;
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( PORT );
	
    
	// Forcefully attaching socket to the port 
	if (bind(server_fd, (struct sockaddr *)&address,sizeof(address))<0)
	{
		cout<<"\t[CLOUD] bind failed"<<endl;
		exit(EXIT_FAILURE);
	}
    cout<<"[CLOUD] bind done"<<endl;
	if (listen(server_fd, 3) < 0)
	{
		cout<<"\t[Cloud] listen fiald ... "<<endl;
		exit(EXIT_FAILURE);
	}
    cout << "[CLOUD] Waiting for client ...." << endl;
    while(true){

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address,(socklen_t*)&addrlen))<0)
        {
            cout<<"\t[CLOUD] accept failed"<<endl;
            exit(EXIT_FAILURE);
        }
        cout<<"------------------------------------------------------"<<endl;
        cout<<"\t[CLOUD] accept new client"<<endl;

        while(true){
            auto maping= metadata_parser();
            
            // recieve the size of the string
            int size = 0;
            if (recv(new_socket, &size, sizeof(size), 0) < 0)
            {
                cout<<"\t[CLOUD] recieve failed"<<endl;
                exit(EXIT_FAILURE);
            }
            string temp(size, 0);

            if(recv(new_socket, &temp[0], size, 0) < 0){
                cout<<"\t[CLOUD] recieve failed"<<endl;
                exit(EXIT_FAILURE);
            }

            if(temp=="Requesting Metadata"){
                cout<<"\t[CLOUD] client sent request => "<<temp<<endl; 

                std::vector<uint8_t> metadata;
                std::for_each(maping.begin(), maping.end(), [&](const auto& entry){
                    const auto& key = entry.first;
                    metadata.reserve(metadata.size() + key.length() + 1);   // reserve enough space for the key plus null terminator
                    std::copy(key.begin(), key.end(), std::back_inserter(metadata));
                    metadata.push_back(0);
                });


                auto metaDataSize = (int)metadata.size() ;
                if (send(new_socket ,  &metaDataSize , sizeof(metaDataSize) , 0) < 0)
                {
                    cout<<"\t[CLOUD] send metadata size failed"<<endl;
                    exit(EXIT_FAILURE);
                }

                if( send(new_socket ,  &metadata[0] , metaDataSize, 0) < 0) 
                {
                    cout<<"\t[CLOUD] Error sending metadata"<<endl;
                }

                cout<<"\t\t [Cloud] send Metadata to client "<<endl;
            }
            else if (temp=="Requesting Package")
            {
                cout<<"\t[CLOUD] client sent request => "<<temp;

                int metaDataSize;
                vector<uint8_t> metadata;
                read( new_socket , &metaDataSize , sizeof(metaDataSize) );
                metadata.resize(metaDataSize);
                read( new_socket , metadata.data() , metaDataSize);
                MetaData md(string(metadata.begin(), metadata.end()));  
                cout<<" => "<<md.get_appName()<<endl;
                // get the file name from the metadata
                string file = maping[md.serializeToJson()];
                // get the dirctory of the file
                string dir = file.substr(0, file.find_last_of('/'));
                // get the app name from the dir
                string app = md.get_appName();
                // read the file ReadAllBytes(file)
                vector<char> packageData = ReadAllBytes((dir+"/"+app+".zip").c_str());

                // search for the metadata in the vector<MetaData>
                auto packageDataSize = (int)packageData.size() ;
                if (send(new_socket ,  &packageDataSize , sizeof(packageDataSize) , 0) < 0)
                {
                    cout<<"\t[CLOUD] send package size failed"<<endl;
                    exit(EXIT_FAILURE);
                }
                std::cout << "packageDataSize: " << packageDataSize << std::endl;
                if(send(new_socket ,  packageData.data() , packageDataSize , 0) < 0) 
                {
                    cout<<"\t Error sending package"<<endl;
                }
                cout<<"\t\t [Cloud] send Package to client "<<endl;

            }
            else 
            {
                if (temp=="End Connection")
                    cout<<"\t[Cloud] Connection ended"<<endl;
                else
                    cout<<"\t[Cloud] Invalid request or Client disconnected "<<endl;
                close(new_socket);
                break;
            }
            
        }
    }
    return 0;
}
