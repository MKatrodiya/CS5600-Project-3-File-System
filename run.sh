#!/bin/bash

# Step 1: Remove the 'testing' directory (forcefully to avoid errors)
rm -rf testing

# Step 2: Create a fresh 'testing' directory
mkdir testing

# Step 3: Build the hw3fuse executable
make hw3fuse

# Step 4: Generate the test image
make test.img

# Step 5: Mount the FUSE filesystem in the background
./hw3fuse -image test.img testing &

# Brief pause to ensure the filesystem is mounted
sleep 1

# Step 6: Navigate into the 'testing' directory and list contents
cd testing && ls -l

# Optional: Unmount the FUSE filesystem after listing (if needed)
# cd ..
# fusermount -u testing
