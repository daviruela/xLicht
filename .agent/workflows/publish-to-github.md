---
description: Publish the xLicht project to GitHub
---

# Publish to GitHub

This workflow pushes the local `xLicht` repository to GitHub.

## Prerequisites
- [x] Git is initialized
- [x] SSH key is configured and added to GitHub
- [ ] Remote repository exists on GitHub

## Steps

### 1. Create Remote Repository
If you haven't already, create a new **empty** repository on GitHub:
1.  Go to [https://github.com/new](https://github.com/new)
2.  Repository name: `xLicht`
3.  **Uncheck** "Initialize this repository with a README" (we already have one).
4.  Click **Create repository**.

### 2. Prepare Local Repository & Push
// turbo-all
1.  Add the remote (if not already added):
    ```powershell
    git remote add origin git@github.com:daviruela/xLicht.git
    ```
    *(If it says "remote origin already exists", you can ignore it or check `git remote -v`)*

2.  Stage and Commit files:
    ```powershell
    git add .
    git commit -m "Initial commit: Project setup with Antigravity"
    ```

3.  Push to GitHub:
    ```powershell
    git branch -M main
    git push -u origin main
    ```
