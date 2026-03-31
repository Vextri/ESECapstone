# CMake IntelliSense Error Fix

## Quick Fix for VS Code IntelliSense Errors

When you see red underlines on includes like `#include "pico/stdlib.h"` but the code compiles fine, follow these steps:

### Step 1: Clean the build cache
```powershell
Remove-Item -Recurse -Force build
```

### Step 2: Regenerate CMake configuration
```powershell
cmake -B build -G Ninja
```

### Step 3: Reload VS Code's C++ configuration
- Press `Ctrl+Shift+P`
- Type: "C/C++: Rescan Workspace"
- Press Enter

### One-Liner Fix
```powershell
Remove-Item -Recurse -Force build; cmake -B build -G Ninja
```
Then reload the C++ workspace in VS Code.

## What Causes This Problem

The issue happens because:
1. CMake caches absolute paths when it first configures your project
2. When you move/copy the project folder, the cached paths become invalid
3. VS Code's IntelliSense relies on these paths to find headers
4. Your code still compiles fine, but IntelliSense shows errors

## Prevention Tips

To avoid this problem:
1. Always delete the `build` folder when moving a project
2. Use relative paths in your CMakeLists.txt (which you're already doing correctly)
3. Don't commit the `build` folder to version control

## Alternative VS Code Commands

If the above doesn't work, try these VS Code commands:
- `C/C++: Reset IntelliSense Database`
- `C/C++: Reload IntelliSense`
- `Developer: Reload Window`

## When This Happens

This typically occurs when:
- Moving the project to a different folder
- Copying the project to another computer
- Working with deeply nested folder structures
- After changing CMakeLists.txt significantly

Remember: If your code compiles successfully but shows red errors in the editor, it's almost always an IntelliSense configuration issue, not a code problem!