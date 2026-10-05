"""NOTYVOS host/build infrastructure map (Python Diagrams).

Render on a machine with Graphviz and the diagrams package:

    pip install diagrams
    python docs/diagrams/host-infrastructure.py

Output is written next to this file as host-infrastructure.png.
This is a host-side map, not a kernel subsystem diagram.
"""

from pathlib import Path

from diagrams import Cluster, Diagram, Edge
from diagrams.onprem.client import Client
from diagrams.onprem.vcs import Git
from diagrams.programming.language import Cpp
from diagrams.generic.os import LinuxGeneralUsage
from diagrams.generic.storage import Storage
from diagrams.onprem.compute import Server


def main() -> None:
    out = Path(__file__).with_suffix("")
    with Diagram(
        "NOTYVOS host infrastructure",
        filename=str(out),
        show=False,
        direction="LR",
        graph_attr={"bgcolor": "transparent"},
    ):
        dev = Client("Developer")
        repo = Git("NotYVOS repo")

        with Cluster("Build host"):
            clang = Cpp("Clang / LLD")
            cmake = Server("CMake + Ninja")
            fonts = Storage("Fonts/Inter")
            limine = Storage("Limine 12.9.0")
            iso = LinuxGeneralUsage("notyvos.iso")

        vbox = Server("VirtualBox")
        guest = LinuxGeneralUsage("NOTYVOS guest")

        dev >> repo >> cmake
        fonts >> cmake
        limine >> cmake
        clang >> cmake
        cmake >> iso >> vbox >> guest
        guest >> Edge(label="COM1 :2323") >> dev


if __name__ == "__main__":
    main()
