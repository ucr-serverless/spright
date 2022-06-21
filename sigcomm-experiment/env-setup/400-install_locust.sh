#/bin/bash

cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

sudo apt update && sudo apt install -y python3-pip

# Install dependencies
pip3 install -r requirements.txt

# Install Locust
pip3 install locust

# export the locust path to locust
echo export PATH="$HOME/.local/bin:$PATH" >> ~/.bashrc
source ~/.bashrc

echo "Installation of Locust is done. Please do source ~/.bashrc before using it!"