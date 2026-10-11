#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#define PORTA 5000
#define FICHAS_INICIAIS 100
#define TAM_MSG 128

#define PEDRA 1
#define PAPEL 2
#define TESOURA 3

// fases
#define FASE_AGUARDANDO 0
#define FASE_APOSTA 1
#define FASE_COBRIR 2
#define FASE_JOGADA 3
#define FASE_FIM 4

// codigos de erro (enviados como "X01", "X02", ...)
#define ERR_APOSTA_INVALIDA 1  // maior que saldo/limite
#define ERR_NAO_E_SUA_VEZ   2
#define ERR_JOGADA_INVALIDA 3
#define ERR_FASE_ERRADA     4
#define ERR_APOSTA_MENOR_1  5
#define ERR_JA_JOGOU        6

typedef struct {
    int fichas[3];   // usar indices 1 e 2
    int aposta[3];
    int jogada[3];   // 0 = ainda nao jogou
    int pote;
    int fase;
    int vez;         // quem deve agir
    int rodada;
    int conectados;
    int ativo;       // 0 = jogo encerrado
} Estado;

#endif
