@ECHO OFF

PATH=c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\;%PATH%

SET LIB=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\lib\;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\

cd %FILE_D%

IF EXIST "%FILE_N%.exe" (
	IF EXIST "%FILE_N%_old.exe" DEL "%FILE_N%_old.exe"
	REN "%FILE_N%.exe" "%FILE_N%_old.exe"
)

SET FILE_NAME="%FILE_N%.obj" 

::@ECHO.
::@ECHO %FILE_N%.obj

Setlocal EnableDelayedExpansion

@ECHO.

IF DEFINED FLAG (
	IF EXIST !FILE_N!.obj (
		link /subsystem:console /nodefaultlib /entry:main %FILE_NAME% "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\Kernel32.Lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\User32.lib" "c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\lib\msvcrt.lib"
	)ELSE (
		@ECHO ERROR LINK_NASM
		EXIT 666
	)
)ELSE (
	SET FILE_OLD="~~~"
	FOR /R %%i in (*) do (
		IF %%~xi==.obj (
			IF NOT "%%~ni"=="!FILE_N!" (
				IF NOT "%%~ni"=="!FILE_N!_old" (
					IF NOT "%%~ni"=="!FILE_OLD!_old" (
						SET FILE_OLD=%%~ni
						SET FILE_NAME=!FILE_NAME!"%%~nxi" 
					)
				)
			)
		)
	)
	IF EXIST !FILE_N!.obj (
		@ECHO !FILE_NAME!
		@ECHO.
		link !FILE_NAME!  "c:\Program Files (x86)\Firebird\Firebird_2_1\lib\fbclient_ms.lib" "c:\Program Files (x86)\Firebird\Firebird_2_1\lib\ib_util_ms.lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\User32.lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\Gdi32.Lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\shell32.lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\ComCtl32.Lib" "c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\comdlg32.lib"
	)ELSE (
		@ECHO ERROR LINK
		EXIT 666
	)
)





