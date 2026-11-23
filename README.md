<div align="center">

# 🎛️ Gerador de Áudio ESP32

**Seis tipos de sinal · Display colorido · Controle por cinco botões**

ESP32-WROOM-32 | TFT ST7789 1,9″ | 20 Hz a 20 kHz | DAC com DMA

</div>

Gerador de áudio com ESP32 para testes de bancada, experimentos com amplificadores e estudo de sinais. A interface em estilo neon mostra o modo selecionado, a frequência ou o corte do filtro, a amplitude, o estado da saída e uma prévia gráfica da forma de onda.

[Recursos](#recursos) · [Montagem](#montagem-e-pinagem) · [Instalação](#instalação) · [Como usar](#como-usar) · [Solução de problemas](#solução-de-problemas)

## Recursos

- Seis modos: seno, triangular, quadrada, serra, ruído branco e ruído rosa.
- Frequência dos tons ajustável entre **20 Hz e 20 kHz**.
- Filtro passa-baixa ajustável nos dois modos de ruído.
- Cinco níveis de amplitude: **10%, 25%, 50%, 75% e 100%**.
- Botão para ativar ou desativar a geração do sinal.
- Ajuste de frequência com repetição e aceleração ao segurar os botões.
- Display em retrato, com barra de amplitude e indicação colorida de saída.
- Atualização por regiões da tela e animação das prévias de ruído.
- Desligamento da iluminação após **60 segundos** sem uso dos botões.
- Geração em tarefa FreeRTOS dedicada, com envio ao DAC por DMA.

## Especificações do firmware

| Item | Configuração |
| --- | --- |
| Placa de referência | ESP32 DevKit V1 com ESP32-WROOM-32 |
| Saída de áudio | Mono, DAC interno no GPIO25 |
| Resolução das amostras | 8 bits |
| Taxa de amostragem configurada | 192.000 amostras/s |
| Faixa do controle | 20 a 20.000 Hz |
| Display | ST7789 de 1,9″, 170 × 320 pixels |
| Orientação | Retrato, rotação 0 |
| Comunicação com o display | SPI de hardware, barramento HSPI |
| Controles | 5 botões com `INPUT_PULLUP` |
| Monitor Serial | 115200 baud |

Os valores acima descrevem a configuração do código. A precisão da frequência, a tensão de saída e a distorção dependem do hardware e precisam de medição para serem caracterizadas.

## Modos de sinal

O botão **MODO** percorre os modos nesta ordem e retorna ao primeiro:

| Modo no display | Sinal gerado | Função dos botões + e − |
| --- | --- | --- |
| `SENO` | Onda senoidal por tabela de 256 pontos | Ajustar a frequência do tom |
| `TRIANGULAR` | Onda triangular | Ajustar a frequência do tom |
| `QUADRADA` | Onda quadrada | Ajustar a frequência do tom |
| `SERRA` | Onda dente de serra | Ajustar a frequência do tom |
| `RUIDO BRANCO` | Ruído pseudoaleatório com filtro passa-baixa | Ajustar o parâmetro de corte do filtro |
| `RUIDO ROSA` | Ruído rosa por Voss–McCartney simplificado, com filtro passa-baixa | Ajustar o parâmetro de corte do filtro |

Nos quatro primeiros modos, a legenda da tela é **FREQUENCIA**. Nos modos de ruído, ela passa a ser **CORTE LP**. O mesmo controle numérico é compartilhado entre os modos.

A prévia gráfica representa o tipo de sinal selecionado. Seu tamanho e seu período desenhado são ilustrativos; os parâmetros efetivos aparecem nos campos numéricos.

## Materiais

| Quantidade | Componente |
| --- | --- |
| 1 | ESP32 DevKit V1 com módulo ESP32-WROOM-32 |
| 1 | Display TFT SPI ST7789 de 1,9″ e resolução 170 × 320 |
| 5 | Botões momentâneos normalmente abertos |
| 1 | Cabo USB de dados para programação e alimentação da placa |
| Conforme a montagem | Fios, protoboard ou placa de circuito |
| Para reprodução sonora | Amplificador com entrada analógica e alto-falante compatível |

Esta pinagem e o firmware foram preparados para o **ESP32 clássico**. Outras famílias, como ESP32-C3 e ESP32-S3, exigem adaptação da geração de áudio e da configuração de hardware.

## Montagem e pinagem

### Display ST7789

| Pino do display | Ligação no ESP32 | Função |
| --- | --- | --- |
| `VCC` | `3V3` | Alimentação do módulo compatível com 3,3 V |
| `GND` | `GND` | Terra |
| `SCL` / `SCK` / `CLK` | `GPIO14` | Clock SPI |
| `SDA` / `MOSI` / `DIN` | `GPIO13` | Dados SPI |
| `CS` | `GPIO23` | Seleção do display |
| `DC` / `A0` | `GPIO16` | Seleção de dados/comandos |
| `RST` / `RES` | `GPIO17` | Reset |
| `BL` / `BLK` | `GPIO26` | Controle da iluminação, ativo em nível alto |

O pino `SDA` desta tabela transporta dados **SPI**. O display usa somente escrita, por isso não há ligação MISO.

A ligação do GPIO26 pressupõe que `BL` seja uma entrada de controle compatível com a corrente do GPIO. Se o módulo expuser diretamente o LED da iluminação, use um estágio de acionamento adequado ao módulo.

### Botões

Ligue um terminal de cada botão ao GPIO indicado e o outro ao **GND**. O firmware ativa os resistores de pull-up internos; o pressionamento é reconhecido em nível baixo.

| Botão | GPIO | Ação |
| --- | --- | --- |
| **MODO** | `GPIO32` | Selecionar o próximo tipo de sinal |
| **AMPLITUDE** | `GPIO33` | Selecionar o próximo nível de amplitude |
| **OUTPUT** | `GPIO27` | Alternar a saída entre ON e OFF |
| **FREQUÊNCIA −** | `GPIO18` | Diminuir a frequência ou o corte LP |
| **FREQUÊNCIA +** | `GPIO19` | Aumentar a frequência ou o corte LP |

### Saída de áudio

| Ligação | Destino |
| --- | --- |
| `GPIO25` | Entrada analógica de áudio do amplificador, com acoplamento adequado |
| `GND` | Terra da entrada de áudio do amplificador |

O DAC fornece sinal para uma entrada de alta impedância. Para acionar alto-falantes ou fones, utilize um estágio amplificador adequado.

As amostras são centradas no valor digital **128**, portanto há componente contínua no pino. Confira se a entrada do amplificador já possui capacitor de acoplamento e dimensione o acoplamento para a impedância de entrada e a faixa de frequência desejada.

## Instalação

### 1. Preparar o Arduino IDE

Instale o Arduino IDE e o pacote de placas **esp32**, de **Espressif Systems**, pelo Gerenciador de Placas. Utilize a família **3.x** do pacote para este código, que inclui o driver `driver/dac_continuous.h` do ESP-IDF 5.

O Arduino-ESP32 2.x utiliza uma base anterior do ESP-IDF. Se aparecer erro de arquivo `dac_continuous.h` ausente, confira a versão do pacote e a placa selecionada.

### 2. Instalar as bibliotecas

No Gerenciador de Bibliotecas, instale:

| Biblioteca | Uso |
| --- | --- |
| **Adafruit GFX Library** | Texto e elementos gráficos |
| **Adafruit ST7735 and ST7789 Library** | Controle do display ST7789 |
| **Adafruit BusIO** | Dependência das bibliotecas Adafruit |

Aceite a instalação das dependências solicitadas. `Arduino.h`, `SPI.h` e os cabeçalhos do ESP-IDF vêm com o ambiente da placa.

### 3. Abrir e gravar o projeto

1. Abra o arquivo [Gerador_Audio_ESP32.ino](Gerador_Audio_ESP32/Gerador_Audio_ESP32.ino), dentro da pasta `Gerador_Audio_ESP32`.
2. Em **Ferramentas → Placa**, selecione **ESP32 Dev Module** para a placa ESP32-WROOM-32 de referência.
3. Selecione a porta serial correspondente à placa.
4. Confira as ligações e mantenha o ganho do amplificador baixo no primeiro teste.
5. Compile e envie o sketch.
6. Abra o Monitor Serial em **115200 baud** para acompanhar a inicialização e as mensagens de estado.

O arquivo `.ino` deve permanecer na pasta com o mesmo nome. A versão exata do pacote ESP32 e das bibliotecas usada na montagem original não foi informada; a indicação acima descreve o ambiente necessário às APIs presentes no sketch.

## Como usar

Ao iniciar ou reiniciar, o firmware carrega:

| Parâmetro | Valor inicial |
| --- | --- |
| Modo | Seno |
| Frequência | 1.000 Hz |
| Amplitude | 100% |
| Saída | ON |
| Iluminação | Ligada |

### Selecionar modo e amplitude

Pressione **MODO** para avançar entre os seis tipos de sinal.

Pressione **AMPLITUDE** para percorrer **10% → 25% → 50% → 75% → 100% → 10%**. Como o aparelho inicia em 100%, o primeiro toque seleciona 10%. O percentual representa a escala digital das amostras; a tensão efetiva depende do circuito.

### Ajustar frequência ou corte do filtro

Pressione **+** ou **−** para alterar o valor. O passo base depende do valor atual:

| Valor atual | Passo base |
| --- | --- |
| 20 a 99 Hz | 1 Hz |
| 100 a 999 Hz | 5 Hz |
| 1.000 a 4.999 Hz | 10 Hz |
| 5.000 a 9.999 Hz | 50 Hz |
| 10.000 a 20.000 Hz | 100 Hz |

Ao manter o botão pressionado, a repetição começa após aproximadamente **450 ms**. A partir de **1,5 segundo**, o passo passa a 5 vezes o base; a partir de **3 segundos**, passa a 20 vezes o base. O intervalo entre repetições também diminui.

### Ativar e desativar a saída

Pressione **OUTPUT** para alternar entre `OUTPUT ON`, em verde, e `OUTPUT OFF`, em vermelho.

Em OFF, o gerador envia amostras constantes de valor **128** ao DAC. O pino permanece polarizado e o amplificador continua alimentado. O código não utiliza um pino de mute do amplificador; um eventual ruído residual precisa ser avaliado junto ao circuito de áudio.

### Iluminação da tela

Após **60 segundos** sem atividade nos botões, a iluminação apaga. O áudio continua conforme o estado de OUTPUT. Qualquer botão reacende a tela e também executa sua função normal.

Os ajustes ficam em memória RAM e retornam aos valores iniciais ao reiniciar.

## Configurações no código

| Identificador | Valor atual | Finalidade |
| --- | --- | --- |
| `FREQ_MIN` | `20` | Limite inferior do controle |
| `FREQ_MAX` | `20000` | Limite superior do controle |
| `SAMPLE_RATE` | `192000` | Taxa solicitada ao driver DAC |
| `AUDIO_BUFFER_SIZE` | `384` | Número de amostras do buffer gerado |
| `BACKLIGHT_TIMEOUT` | `60000UL` | Tempo sem uso, em milissegundos |
| `TFT_WIDTH` / `TFT_HEIGHT` | `170` / `320` | Dimensões usadas na inicialização |
| `TFT_BL_ON` / `TFT_BL_OFF` | `HIGH` / `LOW` | Polaridade do controle da iluminação |
| `amplitudeTable` | `{10, 25, 50, 75, 100}` | Níveis disponíveis de amplitude |

O tempo efetivo da iluminação é definido por `BACKLIGHT_TIMEOUT`. Alguns comentários do arquivo original mencionam 30 segundos, mas a constante está configurada para **60 segundos**.

## Solução de problemas

| Sintoma | O que conferir |
| --- | --- |
| Erro com `driver/dac_continuous.h` | Pacote ESP32 da família 3.x e seleção do ESP32 clássico |
| Erro com `Adafruit_ST7789.h` | Instalação de Adafruit ST7735 and ST7789 Library e dependências |
| Display branco ou sem imagem | Modelo ST7789, resolução 170 × 320, alimentação e sinais CS/DC/RST/SCK/MOSI |
| Cores invertidas | A chamada atual é `tft.invertDisplay(true)`; teste `false` conforme o módulo |
| Tela apagou após um minuto | Pressione um botão para reativar a iluminação |
| Botão não responde | Ligação entre o GPIO correto e GND; pressionado deve produzir nível LOW |
| Sem sinal de áudio | Estado OUTPUT, amplitude, GPIO25, terra comum e entrada do amplificador |
| Som baixo ou distorcido | Ganho do amplificador, alimentação, carga e acoplamento da entrada |
| Ruído com OUTPUT OFF | Polarização do DAC, alimentação, aterramento e circuito de mute do amplificador |
| Tela mostra `ERRO DAC` ou `ERRO TASK` | Mensagem detalhada no Monitor Serial em 115200 baud |

## Arquivos

| Caminho | Conteúdo |
| --- | --- |
| `README.md` | Apresentação, pinagem e instruções do projeto |
| `Gerador_Audio_ESP32/Gerador_Audio_ESP32.ino` | Sketch enviado como base, preservado integralmente |
| `PUBLICAR_NO_GITHUB.md` | Guia de envio do projeto e descrição sugerida para o repositório |

## Referências

- [Arduino-ESP32: migração da versão 2.x para 3.0](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html)
- [ESP-IDF 5.1: DAC do ESP32 e modo contínuo](https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/peripherals/dac.html)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit ST7735 and ST7789 Library](https://github.com/adafruit/Adafruit-ST7735-Library)
- [Adafruit: inicialização do ST7789 de 1,9″ em 170 × 320](https://learn.adafruit.com/adafruit-1-9-color-ips-tft-display/arduino-wiring-test)

---

Documentação conferida com o sketch fornecido. Esta preparação não inclui compilação nem ensaio em hardware.
