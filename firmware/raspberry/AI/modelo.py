import torch.nn as nn


class FireNet(nn.Module):

    def __init__(self):
        super().__init__()

        # Produces 16 grids, each resulting from a filter learned by the network,
        # showing where that feature appears in the image.
        # We now have 16 grids instead of the original image, with the same width
        # and height as the image, each telling us something about it.
        self.conv1 = nn.Conv2d(3, 16, 3, padding=1)

        self.conv2 = nn.Conv2d(16, 32, 3, padding=1)

        self.conv3 = nn.Conv2d(32, 64, 3, padding=1)

        self.pool = nn.MaxPool2d(2)

        # At each position, 5 filters combine the same 64 values into 5 outputs.
        self.output = nn.Conv2d(64, 5, kernel_size=1)

        self.relu = nn.ReLU()

    def forward(self, x):

        x = self.relu(self.conv1(x))

        x = self.pool(x)

        x = self.relu(self.conv2(x))

        x = self.pool(x)

        x = self.relu(self.conv3(x))

        x = self.output(x)

        return x
