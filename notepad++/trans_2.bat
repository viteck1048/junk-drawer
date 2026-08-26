@ECHO OFF

PATH=c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\;%PATH%

SET INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\include\;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include\

cd %FILE_D%

IF EXIST "%FILE_N%.obj" (
	IF EXIST "%FILE_N%_old.obj" DEL "%FILE_N%_old.obj"
	REN "%FILE_N%.obj" "%FILE_N%_old.obj"
) 

::@ECHO ON

cl.exe /c /W3 /EHsc %FILE_NAME%
