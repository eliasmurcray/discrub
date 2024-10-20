#!/bin/bash

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null
then
    echo "clang-format could not be found. Installing clang-format..."
    
    # Install clang-format based on the package manager
    if [ -x "$(command -v apt-get)" ]; then
        sudo apt-get update
        sudo apt-get install -y clang-format
    elif [ -x "$(command -v yum)" ]; then
        sudo yum install -y clang-format
    elif [ -x "$(command -v brew)" ]; then
        brew install clang-format
    else
        echo "Package manager not found. Please install clang-format manually."
        exit 1
    fi
fi

# Format the files
for file in src/*.c include/*.h; do
    clang-format -i "$file"
    echo "Formatted: $file"
done

echo "Formatting complete."
