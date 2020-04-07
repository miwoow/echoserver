cmake_minimum_required(VERSION 2.8)
PROJECT(EchoServer C)

AUX_SOURCE_DIRECTORY(
./src/
DIRA
)

SET(DIR_SRCS
${DIRA}
)

ADD_EXECUTABLE(EchoServer ${DIR_SRCS})

