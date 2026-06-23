#!/bin/bash
# Setup script to initialize GitHub repository and push initial commit

set -e

echo "=========================================="
echo "LuxSync DMX - GitHub Setup"
echo "=========================================="

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if already a git repo
if [ -d ".git" ]; then
    echo -e "${YELLOW}✓ Already a git repository${NC}"
    git remote -v
else
    echo -e "${RED}✗ Not a git repository yet${NC}"
    read -p "Enter your GitHub username: " GITHUB_USER
    read -p "Enter repository name [dmx-vst]: " REPO_NAME
    REPO_NAME=${REPO_NAME:-dmx-vst}
    
    REPO_URL="https://github.com/${GITHUB_USER}/${REPO_NAME}.git"
    
    echo ""
    echo "Repository URL: $REPO_URL"
    echo ""
    echo "Make sure this repository exists on GitHub before continuing."
    read -p "Press Enter to continue or Ctrl+C to cancel..."
    
    # Initialize git
    echo "Initializing git repository..."
    git init
    git config user.name "$(git config --global user.name || echo 'LuxSync Developer')"
    git config user.email "$(git config --global user.email || echo 'dev@luxsync.local')"
    
    git add .
    git commit -m "Initial commit: LuxSync DMX VST3 + AI Automator"
    git branch -M main
    git remote add origin "$REPO_URL"
fi

echo ""
echo "=========================================="
echo -e "${GREEN}Ready to push!${NC}"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. git push -u origin main"
echo "2. Create a release tag: git tag -a v0.1.0 -m 'Initial release'"
echo "3. git push origin v0.1.0"
echo ""
echo "Then GitHub Actions will automatically:"
echo "  • Build on Windows (VST3 + Standalone)"
echo "  • Build on macOS (AU/VST3 + Standalone for arm64 & x86_64)"
echo "  • Create a GitHub Release with all installers"
echo ""
