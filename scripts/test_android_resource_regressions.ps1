$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputDirectory = Join-Path $projectRoot '.tools\android-resource-regressions'
$classesDirectory = Join-Path $outputDirectory 'classes'
$null = New-Item -ItemType Directory -Path $classesDirectory -Force

& javac -encoding UTF-8 -d $classesDirectory `
    (Join-Path $projectRoot 'app\src\main\java\org\libsdl\app\HIDDeviceIdAllocator.java') `
    (Join-Path $projectRoot 'tests\HIDDeviceIdAllocatorTest.java') `
    (Join-Path $projectRoot 'app\src\main\java\com\dingoopie\android\StagedUploadFile.java') `
    (Join-Path $projectRoot 'tests\StagedUploadFileTest.java')
if ($LASTEXITCODE -ne 0) {
    throw 'Android resource regression compilation failed.'
}

foreach ($testClass in @(
        'org.libsdl.app.HIDDeviceIdAllocatorTest',
        'com.dingoopie.android.StagedUploadFileTest')) {
    & java -cp $classesDirectory $testClass
    if ($LASTEXITCODE -ne 0) {
        throw "Android resource regression failed: $testClass"
    }
}
