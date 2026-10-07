#!/bin/zsh
# script to commit to repo.
echo "${1}"

echo "Add changes in files or directory..."
git add .
echo "committing..."
git commit -m "${1}"
git pull
git push --all
echo "All done! :)"
