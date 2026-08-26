@ECHO OFF

CD %FILE_D%

IF DEFINED FLAG (
	IF EXIST "%FILE_N%_gcc.exe" (
		START "%FILE_D%\%FILE_N%_gcc.exe" "%FILE_N%_gcc.exe"
	)ELSE (
		@ECHO ERROR RUN_gcc
		EXIT 666
	)
)ELSE (
	IF EXIST "%FILE_N%.exe" (
		START "%FILE_D%\%FILE_N%.exe" "%FILE_N%.exe"
	)ELSE (
		@ECHO ERROR RUN
		EXIT 666
	)
)
