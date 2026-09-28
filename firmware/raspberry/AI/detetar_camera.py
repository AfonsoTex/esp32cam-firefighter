"""Abre a webcam do PC e corre o FireNet em tempo real sobre cada frame.

Carrega sempre o "melhor modelo" porque o treinar.py só regrava o
firenet_grid.pt quando a pontuacao de validacao melhora - por isso o
ficheiro que la esta e sempre o melhor que ja treinaste, nao precisamos
de procurar entre varios checkpoints.

Correr, de dentro da pasta do projeto (com o venv-treino ativo):
    python detetar_camera.py
Sai com a tecla "q" ou ESC.
"""
import argparse
import time
from collections import deque

import cv2
import torch

import treinar
from dados import IMAGE_SIZE
from modelo import FireNet
from treinar import CHECKPOINT, get_boxes


# Filtro temporal: so anuncia FOGO se tiver detetado em pelo menos
# MINIMO dos ultimos HISTORICO frames. Serve para o aviso nao piscar
# quando a probabilidade anda a passear a volta do limiar.
HISTORICO = 5

MINIMO = 3


def preprocess(frame):
    # ANTES: frame da webcam, BGR, altura x largura originais (ex.: 480x640x3).
    # OPERACAO: mesma sequencia usada no treino (criar_cache.py) - converter
    # para RGB, redimensionar para 128x128 (a proporcao original nao e
    # preservada, tal como no treino) e normalizar os pixeis para 0..1.
    # DEPOIS: tensor 1x3x128x128, o formato que o FireNet espera.
    image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    image = cv2.resize(image, (IMAGE_SIZE, IMAGE_SIZE))

    tensor = torch.from_numpy(image).float() / 255.0
    tensor = tensor.permute(2, 0, 1)
    tensor = tensor.unsqueeze(0)

    return tensor


def draw_detections(frame, boxes):
    height, width = frame.shape[:2]

    for x, y, box_width, box_height in boxes:
        # x, y, box_width, box_height vem normalizado (0..1), independente
        # do tamanho do frame - por isso multiplicamos pela largura/altura
        # reais da imagem da webcam, e nao pelos 128x128 usados so na
        # entrada do modelo.
        x_min = int((x - box_width / 2) * width)
        y_min = int((y - box_height / 2) * height)
        x_max = int((x + box_width / 2) * width)
        y_max = int((y + box_height / 2) * height)

        x_min, x_max = max(0, x_min), min(width - 1, x_max)
        y_min, y_max = max(0, y_min), min(height - 1, y_max)

        cv2.rectangle(frame, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
        cv2.putText(frame, "fogo", (x_min, max(0, y_min - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    return frame


def open_camera(index):
    # ANTES: nenhuma ligacao a camara.1
    # OPERACAO: tenta abrir com dois "backends" diferentes do Windows
    # (MSMF, depois DSHOW) porque uma webcam pode responder a isOpened()
    # com True num deles e mesmo assim nunca entregar um frame valido -
    # foi o que aconteceu na ultima tentativa (DSHOW sozinho). Para cada
    # backend, tenta ler ate 30 frames (~3s) antes de desistir, porque
    # alguns drivers precisam de um instante para "aquecer" apos abrir.
    # DEPOIS: capture pronta a usar, ou None se nenhum backend resultou.
    backends = [
        ("MSMF", cv2.CAP_MSMF),
        ("DSHOW", cv2.CAP_DSHOW),
        ("padrao", cv2.CAP_ANY),
    ]

    for name, backend in backends:
        print(f"A tentar abrir a camara {index} com o backend {name}...")
        capture = cv2.VideoCapture(index, backend)

        if not capture.isOpened():
            capture.release()
            continue

        for _ in range(30):
            ok, frame = capture.read()
            if ok and frame is not None:
                print(f"Camara aberta com o backend {name}.")
                return capture
            time.sleep(0.1)

        capture.release()

    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", type=int, default=0,
                         help="indice da camara (0 = a primeira/padrao)")
    parser.add_argument("--threshold", type=float, default=0.4,
                         help="confianca minima para desenhar uma deteccao")
    parser.add_argument("--gravar", type=str, default=None,
                         help="grava o video para este ficheiro, ex.: sala.mp4")
    parser.add_argument("--com-caixas", action="store_true",
                         help="grava a imagem com as caixas e o texto por cima "
                              "(sem isto grava a imagem limpa da camara, que e "
                              "o que serve para treinar)")
    args = parser.parse_args()

    # get_boxes (definido em treinar.py) le CONFIDENCE_THRESHOLD do modulo
    # train no momento em que corre - por isso, para o --threshold ter
    # efeito, atualizamos a constante do modulo em vez de passar um
    # argumento (a funcao nao aceita um).
    treinar.CONFIDENCE_THRESHOLD = args.threshold

    model = FireNet()
    checkpoint = torch.load(CHECKPOINT, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["weights"])
    model.eval()

    print(f"Modelo carregado de {CHECKPOINT} (score guardado: {checkpoint['score']:.2f}%)")

    capture = open_camera(args.camera)

    if capture is None:
        raise RuntimeError(
            f"Nao consegui abrir a camara {args.camera} com nenhum backend. "
            "Verifica em Definicoes > Privacidade e seguranca > Camara se "
            "'Permitir que as aplicacoes de ambiente de trabalho acedam a "
            "camara' esta ativo, fecha outras apps que possam esta-la a "
            "usar (Teams, Camara, um separador do browser), e tenta outro "
            "indice com --camera 1."
        )

    print("A correr. Prime 'q' ou ESC na janela do video para sair.")

    # Guarda os ultimos HISTORICO resultados (True/False). deque com
    # maxlen deita fora o mais antigo sozinho quando enche.
    historico = deque(maxlen=HISTORICO)

    # VideoWriter e o gravador do OpenCV: recebe frames um a um e escreve
    # o ficheiro de video. So o criamos quando --gravar foi pedido, e so
    # depois do primeiro frame, porque precisamos do tamanho real da
    # imagem que a camara esta a dar.
    writer = None

    try:
        while True:
            ok, frame = capture.read()

            if not ok:
                print("Nao consegui ler mais frames da camara.")
                break

            # Copia da imagem limpa, antes de lhe desenharmos por cima.
            # E esta que gravamos quando queremos frames para treinar.
            limpo = frame.copy()

            with torch.no_grad():
                prediction = model(preprocess(frame))[0]

            # ANTES: prediction[0] -> 32 x 32 numeros em bruto, um por
            #        posicao espacial da grelha.
            # OPERACAO: sigmoide converte cada um numa probabilidade 0..1;
            #        max() fica com a posicao mais confiante da imagem.
            # DEPOIS: 1 numero. E este que anda a passear a volta do
            #        limiar e faz a caixa piscar - por isso mostramo-lo.
            melhor = torch.sigmoid(prediction[0]).max().item()

            boxes = get_boxes(prediction)

            historico.append(len(boxes) > 0)

            # So conta como fogo se apareceu em MINIMO dos ultimos frames.
            estavel = sum(historico) >= MINIMO

            frame = draw_detections(frame, boxes)

            status = f"FOGO DETETADO ({len(boxes)})" if estavel else "sem fogo"
            color = (0, 0, 255) if estavel else (0, 200, 0)
            cv2.putText(frame, status, (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                        0.9, color, 2)

            # Numero de cima: a confianca mais alta desta imagem.
            # Numero de baixo: o limiar a partir do qual conta como fogo.
            cv2.putText(frame, f"max {melhor:.2f}  limiar {args.threshold:.2f}"
                        f"  {sum(historico)}/{len(historico)}",
                        (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 0), 2)

            if args.gravar:

                if writer is None:
                    altura, largura = frame.shape[:2]

                    # Alguns drivers devolvem 0 em CAP_PROP_FPS; nesse
                    # caso assumimos 20 frames por segundo.
                    fps = capture.get(cv2.CAP_PROP_FPS)
                    if not fps or fps <= 1:
                        fps = 20.0

                    # mp4v e o codec; tem de bater certo com a extensao
                    # .mp4 do ficheiro.
                    codec = cv2.VideoWriter_fourcc(*"mp4v")

                    writer = cv2.VideoWriter(args.gravar, codec, fps,
                                             (largura, altura))

                    print(f"A gravar para {args.gravar} "
                          f"({largura}x{altura}, {fps:.0f} fps)")

                writer.write(frame if args.com_caixas else limpo)

            cv2.imshow("FireNet - deteccao ao vivo", frame)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):  # 27 = ESC
                break
    finally:
        if writer is not None:
            writer.release()
            print(f"Video gravado: {args.gravar}")

        capture.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
