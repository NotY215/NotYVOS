"""NOTYVOS host/build infrastructure map using Python Diagrams.

Compatible source syntax for Python 3.x without version-specific language
features. The installed "diagrams" package and Graphviz have their own
supported Python/platform requirements.

Install:
    python -m pip install diagrams

Render:
    python docs/diagrams/host-infrastructure.py

The PNG is written beside this source file. This is a host-side build map,
not a kernel subsystem diagram.
"""

from __future__ import print_function

import os

from diagrams import Cluster, Diagram, Edge
from diagrams.generic.os import LinuxGeneralUsage
from diagrams.generic.storage import Storage
from diagrams.onprem.client import Client
from diagrams.onprem.compute import Server
from diagrams.onprem.vcs import Git
from diagrams.programming.language import Cpp


def main():
    """Build the host/build/VM infrastructure diagram."""
    output = os.path.splitext(os.path.abspath(__file__))[0]

    with Diagram(
        "NOTYVOS host infrastructure",
        filename=output,
        show=False,
        direction="LR",
        graph_attr={"bgcolor": "transparent"},
    ):
        developer = Client("Developer")
        repository = Git("NotYVOS repo")

        with Cluster("Build host"):
            clang = Cpp("Clang / LLD")
            cmake = Server("CMake + Ninja")
            fonts = Storage("Fonts / Inter")
            limine = Storage("Limine 12.9.0")
            iso = LinuxGeneralUsage("notyvos.iso")

        virtualbox = Server("VirtualBox")
        guest = LinuxGeneralUsage("NOTYVOS guest")

        developer >> repository >> cmake
        fonts >> cmake
        limine >> cmake
        clang >> cmake
        cmake >> iso >> virtualbox >> guest
        guest >> Edge(label="COM1 :2323") >> developer


if __name__ == "__main__":
    main()
