cd D:\Workspace\valentina-develop\valentina-develop


.\build.ps1 -QtQmakePath "D:\Qt\6.10.2\msvc2022_64\bin\qmake.exe"

ou
powershell -ExecutionPolicy Bypass -File .\build.ps1 -QtQmakePath "D:\Qt\6.10.2\msvc2022_64\bin\qmake.exe"



Depois de buildar, para criar o instalador faça,


& "C:\Users\55219\AppData\Local\Programs\Inno Setup 6\ISCC.exe" `
  /DMyAppVersion=0.1.0 `
  /DMyAppStatus= `
  /DMyAppId="{{7081AEC7-38FC-479F-B712-DB073BB76512}}" `
  /DMyAppCopyright="Open Source" `
  /DMyAppArchitecture=x64os `
  /DMyAppMinWinVersion=10.0.17763 `
  /DbuildDirectory="D:\Workspace\valentina-develop\valentina-develop\build\release\install-root\valentina" `
  /DDependencyVCRedist=true `
  /DInnoLanguagesPath="D:\Workspace\valentina-develop\valentina-develop\dist\win\inno\Languages" `
  "D:\Workspace\valentina-develop\valentina-develop\dist\win\inno\valentina.iss"


