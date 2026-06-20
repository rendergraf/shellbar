# Copr Setup for ShellBar

## 1. Create the Copr project

Go to https://copr.fedorainfracloud.org/coprs/rendergraf/
Click "New Project" → name it `shellbar`

Or via CLI once you have an API token:

```sh
# Get API token at https://copr.fedorainfracloud.org/api/
export COPR_TOKEN="your-token-here"

# Create the project
curl -X POST "https://copr.fedorainfracloud.org/api_3/project/add/rendergraf" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "shellbar",
    "description": "A Ghostty-like terminal emulator with a configurable command toolbar",
    "instructions": "",
    "chroots": ["fedora-rawhide-x86_64", "fedora-41-x86_64", "fedora-40-x86_64"],
    "contact": "xavieraraque@gmail.com",
    "homepage": "https://github.com/rendergraf/shellbar"
  }'
```

## 2. Build SRPM and submit

```sh
# Install rpm-build if not already
sudo dnf install rpm-build rpmdevtools

# Build SRPM from the spec
rpmbuild -bs shellbar.spec --define "_sourcedir $(pwd)" --define "_srcrpmdir $(pwd)/build"

# Submit to Copr
copr-cli build shellbar build/shellbar-*.src.rpm
```

## 3. Users install with:

```sh
sudo dnf copr enable rendergraf/shellbar
sudo dnf install shellbar
```
