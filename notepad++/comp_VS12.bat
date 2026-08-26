PATH=c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\;%PATH%

SET INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\include\;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include\

SET LIB=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\lib\;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\

cd %FILE_D%

cl.exe /c /W3 %FILE_NAME%

FOR /F "delims=." %%i in (%FILE_NAME%) do SET FILE_N=%%i

SET FILE_NAME=%FILE_N%.obj 

link "%FILE_NAME%" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\User32.lib"

