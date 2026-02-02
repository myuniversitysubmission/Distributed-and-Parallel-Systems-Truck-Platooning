@echo off

echo Running client...
client.exe 21
set EXITCODE=%ERRORLEVEL%

echo Client exited with %EXITCODE%

if %EXITCODE%==32 (
    echo Building server...
    gcc -Iinclude -DBUILD_SERVER src/server.c src/frame.c src/queue.c src/logical_clock.c -o server.exe -lws2_32
    exit /b 0
)

echo Normal exit.
exit /b 0