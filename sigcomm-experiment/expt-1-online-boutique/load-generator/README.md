## Install Locust load generator
```
# Install pip3
sudo apt update && sudo apt install -y python3-pip

# Install dependencies
pip3 install -r requirements.txt

# Install Locust
pip3 install locust

# Edit .bashrc and export the path to locust
export PATH="$HOME/.local/bin:$PATH"

source ~/.bashrc

# Check whether locust has been installed
locust --version
```