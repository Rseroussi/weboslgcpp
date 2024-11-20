# weboslgcpp
This is my attempt at a c++ implementation for an lg tv websocket library. I based it off of https://github.com/TheRealLink/pylgtv. I'm using websocketpp for the websocket handler which isn't the best from what I've gathered but works just fine for this application. 
## install required libraries

`sudo apt install libwebsocketpp-dev`

`sudo apt install nlohmann-json3-dev`


## build instructions
first clone the repo:

`git clone https://github.com/Rseroussi/weboslgcpp.git`

then navigate into the folder and create a build directory. cmake and make the project.

`cd weboslgcpp`

`mkdir build && cd build`

`cmake .. && make`

currently the main file for testing is in the weboscpp file. Run the generated executable:

`./webos_client`

