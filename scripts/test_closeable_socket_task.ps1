$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputDirectory = Join-Path $projectRoot '.tools\closeable-socket-task'
$classesDirectory = Join-Path $outputDirectory 'classes'
$null = New-Item -ItemType Directory -Path $classesDirectory -Force

& javac -encoding UTF-8 -d $classesDirectory `
    (Join-Path $projectRoot 'app\src\main\java\com\dingoopie\android\CloseableSocketTask.java') `
    (Join-Path $projectRoot 'tests\CloseableSocketTaskTest.java')
if ($LASTEXITCODE -ne 0) {
    throw 'Closeable socket task test compilation failed.'
}

& java -cp $classesDirectory com.dingoopie.android.CloseableSocketTaskTest
if ($LASTEXITCODE -ne 0) {
    throw 'Closeable socket task regression failed.'
}
