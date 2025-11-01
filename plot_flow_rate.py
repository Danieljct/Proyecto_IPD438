#!/usr/bin/env python3
"""
plot_flow_rate.py
------------------
Lee el archivo CSV generado por fattree_k4_replay.cc (por defecto flow_rate.csv)
 y grafica la tasa total agregada que atraviesa el enlace.
"""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def main() -> None:
    parser = argparse.ArgumentParser(description="Grafica la tasa total observada en el enlace")
    parser.add_argument("--input", default="flow_rate.csv", help="CSV con columnas time_s,total_rate_gbps,ecn_marks")
    parser.add_argument("--output", default=None, help="Nombre de archivo para guardar la figura (PNG/PDF). Si no se indica, muestra la ventana interactiva.")
    args = parser.parse_args()

    csv_path = Path(args.input)
    if not csv_path.is_file():
        raise FileNotFoundError(f"No existe el archivo {csv_path}")

    df = pd.read_csv(csv_path)
    if df.empty:
        raise ValueError("El CSV está vacío; ejecuta la simulación antes de graficar")

    df = df.sort_values("time_s")

    plt.figure(figsize=(10, 6))
    plt.plot(df["time_s"], df["total_rate_gbps"], label="Total")

    if "ecn_marks" in df.columns:
        marked = df[df["ecn_marks"] > 0]
        if not marked.empty:
            plt.scatter(marked["time_s"], marked["total_rate_gbps"], color="red", label="Marcas ECN", zorder=5)

    plt.xlabel("Tiempo (s)")
    plt.ylabel("Tasa total (Gbps)")
    plt.title("Tasa agregada en el enlace")
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.tight_layout()

    if args.output:
        plt.savefig(args.output, dpi=150)
        print(f"Figura guardada en {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
