#!/usr/bin/env python3
"""wavelet_reconstruct.py
--------------------------
Toma la señal agregada producida por ``fattree_k4_replay`` (``flow_rate.csv``)
 y aplica una descomposición Haar equivalente al bloque ``Wavelet::counter`` del
 proyecto WaveSketch. Reconstruye la señal a partir de los coeficientes y
 grafica, en un mismo lienzo, la serie original y la reconstruida para validar
 la fidelidad de la transformación.

La implementación sigue la idea central del módulo ``wavesketch/Wavelet``:
- Se trabaja con ventanas equiespaciadas.
- Se utiliza la base Haar (promedio/detalle) para construir coeficientes.
- La reconstrucción invierte cada etapa para recuperar la señal original.

El script acepta archivos distintos y permite guardar la figura resultante.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


@dataclass
class WaveletCoefficients:
    """Coeficientes Haar jerárquicos."""

    approx: np.ndarray
    details: List[np.ndarray]

    def reconstruct(self) -> np.ndarray:
        """Reconstruye la señal original invirtiendo el proceso Haar."""
        current = self.approx.copy()
        for detail in reversed(self.details):
            if current.shape[0] != detail.shape[0]:
                raise ValueError("Dimensiones incompatibles entre aproximación y detalle")
            next_level = np.empty(detail.shape[0] * 2, dtype=current.dtype)
            next_level[0::2] = current + detail
            next_level[1::2] = current - detail
            current = next_level
        return current


def next_power_of_two(n: int) -> int:
    """Obtiene la siguiente potencia de dos >= n."""
    if n <= 0:
        raise ValueError("El tamaño debe ser positivo")
    return 1 if n == 1 else 1 << (n - 1).bit_length()


def haar_forward(signal: np.ndarray) -> WaveletCoefficients:
    """Realiza la transformada Haar directa sobre la señal."""
    current = signal.astype(float, copy=True)
    details: List[np.ndarray] = []
    length = current.shape[0]

    while length > 1:
        view = current[:length]
        avg = 0.5 * (view[0::2] + view[1::2])
        diff = 0.5 * (view[0::2] - view[1::2])
        details.append(diff)
        current[: avg.shape[0]] = avg
        length = avg.shape[0]

    approx = current[:length].copy()
    return WaveletCoefficients(approx=approx, details=details)


def prepare_signal(values: Sequence[float]) -> Tuple[np.ndarray, int]:
    """Ajusta la señal al tamaño esperado (potencia de dos)."""
    array = np.asarray(values, dtype=float)
    orig_len = array.shape[0]
    target_len = next_power_of_two(orig_len)
    if target_len != orig_len:
        pad = np.full(target_len - orig_len, array[-1], dtype=array.dtype)
        array = np.concatenate([array, pad])
    return array, orig_len


def main() -> None:
    parser = argparse.ArgumentParser(description="Descompone y reconstruye la señal total usando wavelets Haar")
    parser.add_argument("--input", default="flow_rate.csv", help="CSV con columnas time_s,total_rate_gbps")
    parser.add_argument("--output", default="wavelet_reconstruction.png", help="Ruta para guardar la figura resultante")
    parser.add_argument("--column", default="total_rate_gbps", help="Columna a analizar dentro del CSV")
    args = parser.parse_args()

    csv_path = Path(args.input)
    if not csv_path.is_file():
        raise FileNotFoundError(f"No existe el archivo {csv_path}")

    df = pd.read_csv(csv_path)
    if args.column not in df.columns:
        raise ValueError(f"La columna '{args.column}' no existe en {csv_path}")

    signal, original_length = prepare_signal(df[args.column].to_numpy())
    coeffs = haar_forward(signal)
    reconstructed = coeffs.reconstruct()

    # Limitar a la longitud original (sin padding)
    original_series = signal[:original_length]
    reconstructed_series = reconstructed[:original_length]
    time_axis = df["time_s"].to_numpy()[:original_length]

    plt.figure(figsize=(12, 6))
    plt.plot(time_axis, original_series, label="Original", linewidth=2)
    plt.plot(time_axis, reconstructed_series, label="Reconstruida", linestyle="--")
    plt.xlabel("Tiempo (s)")
    plt.ylabel("Tasa total (Gbps)")
    plt.title("Comparación señal original vs reconstruida (Wavelet Haar)")
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.tight_layout()
    plt.savefig(args.output, dpi=150)
    print(f"Figura guardada en {args.output}")


if __name__ == "__main__":
    main()
