#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 16

typedef struct Pagina_{
	int idPagina;
	int escritas;
	int usos;

} Pagina;

typedef struct Buffer_{
	int idBuffer;
	Pagina *pagina;
} Buffer;

typedef struct ElementoFantasma_{
	int idPaginaFantasma;
	struct ElementoFantasma_ *cauda;
} ElementoFantasma;

typedef struct ListaFantasma_{
	ElementoFantasma *inicio;
	ElementoFantasma *fim;

	int tamanhoAtual;
	int tamanhoMaximo;
} ListaFantasma;


typedef struct ElementoReal_{
	Buffer *bufferCabeca;
	struct ElementoReal_ *cauda;
} ElementoReal;

typedef struct ListaReal_{
	ElementoReal *inicio;
	ElementoReal *fim;

	int tamanhoAtual;
	int tamanhoMaximo;
} ListaReal;

typedef struct AreaBuffer_{
	ListaReal *RA;
	ListaReal *FA;
	ListaReal *livres;

	ListaFantasma *GRA;
	ListaFantasma *GFA;

	int tamanhoAtual;
	int tamanhoMaximo;

	int tamanhoMaximoFantasma;
	int tamanhoAtualFantasma;
} AreaBuffer;

typedef struct {
	AreaBuffer *areaEscrita,*areaLeitura;	
	Buffer *buffers[BUFFER_SIZE];
	int conselheiroAreaEscrita; //positivo aumenta área de escrita; negativo aumenta de leitura;
	int conselheiroFrequenciaEscrita;////positivo aumenta lista de frequência de escrita; negativo aumenta de recencia;
	int conselheiroFrequenciaLeitura;////positivo aumenta lista de frequência de leitura; negativo aumenta de recencia;
}BufferPool;
int realInserirBuffer(AreaBuffer *area,int list ,Buffer *buffer);


BufferPool *bufferPool;

void initListaReal(ListaReal *lista, int tamanhoMaximo){
	
	lista->inicio = NULL;
	lista->fim = NULL;
	lista->tamanhoAtual = 0;
	lista->tamanhoMaximo = tamanhoMaximo;
}

void initListaFantasma(ListaFantasma *lista, int tamanhoMaximo){
	lista->inicio = NULL;
	lista->fim = NULL;
	lista->tamanhoAtual = 0;
	lista->tamanhoMaximo = tamanhoMaximo;
}

void initArea(AreaBuffer *area, int tamanhoMaximo){
	area->RA = malloc(sizeof(ListaReal));//0
	area->FA = malloc(sizeof(ListaReal));//1
	area->livres = malloc(sizeof(ListaReal));

	area->GRA = malloc(sizeof(ListaFantasma));//0
	area->GFA = malloc(sizeof(ListaFantasma));//1

	initListaReal(area->RA , ((int)tamanhoMaximo/2));
	initListaReal(area->FA , ((int)tamanhoMaximo/2));
	initListaReal(area->livres , tamanhoMaximo);

	initListaFantasma(area->GRA , ((int)tamanhoMaximo/2));
	initListaFantasma(area->GFA , ((int)tamanhoMaximo/2));

	area->tamanhoMaximo = tamanhoMaximo;
	area->tamanhoAtual = 0;
}

void initBufferPool(BufferPool* bufferPool){
	bufferPool->areaEscrita = malloc(sizeof(AreaBuffer));
	bufferPool->areaLeitura = malloc(sizeof(AreaBuffer));

	initArea(bufferPool->areaEscrita,(int)BUFFER_SIZE/2);
	initArea(bufferPool->areaLeitura,(int)BUFFER_SIZE/2);

	bufferPool->conselheiroFrequenciaLeitura = 0;
	bufferPool->conselheiroFrequenciaEscrita = 0;
	bufferPool->conselheiroAreaEscrita = 0;
	
	for(int i = 0 ; i < (int)BUFFER_SIZE/2 ; i++){
		bufferPool->buffers[i] = malloc(sizeof(Buffer));
		bufferPool->buffers[i + (int)BUFFER_SIZE/2] = malloc(sizeof(Buffer));
		
		bufferPool->buffers[i]->idBuffer = i;
		bufferPool->buffers[i]->pagina = NULL;

		bufferPool->buffers[i + (int)BUFFER_SIZE/2]->idBuffer = i + (int)BUFFER_SIZE/2;
		bufferPool->buffers[i + (int)BUFFER_SIZE/2]->pagina = NULL;
		
		realInserirBuffer(bufferPool->areaEscrita,2,bufferPool->buffers[i] );
		realInserirBuffer(bufferPool->areaLeitura,2,bufferPool->buffers[i + (int)BUFFER_SIZE/2] );
	}

}


//Manipulação Lista Fantasma
ListaFantasma* getListFantasma(AreaBuffer *area,int list){
	if(list == 0)//GRA
		return area->GRA;
	else if(list == 1)//GFA
		return area->GFA;
	else 
		return NULL;
}


int fantasmaContem(AreaBuffer *area, int list, int idPagina){
	ListaFantasma *lista = getListFantasma(area,list);
	ElementoFantasma *elemento = lista->inicio;
	while(elemento != NULL){
		if(elemento->idPaginaFantasma == idPagina)
			return 1;
		elemento = elemento->cauda;
	}
	return 0;
}

int fantasmaRemoveLRU(AreaBuffer *area, int list){
	ListaFantasma *lista = getListFantasma(area,list);
	ElementoFantasma *ret = NULL;
	if(lista->tamanhoAtual > 0){
		ret = lista->inicio;
		if(lista->tamanhoAtual == 1){
			lista->inicio = NULL;
			lista->fim = NULL;
		}else{
			lista->inicio = ret->cauda;
		}
		lista->tamanhoAtual--;
		area->tamanhoAtualFantasma--;
		int id = ret->idPaginaFantasma;
		free(ret);
		// printf("Removido do fantasma %d\n",id);
		return id;
	}
	return -1;
}

int fantasmaRemovePorIdPagina(AreaBuffer *area, int list, int idPagina){
	ListaFantasma *lista = getListFantasma(area,list);
	
	if(lista->tamanhoAtual == 0)
		return -1;
	
	if(lista->tamanhoAtual == 1 && lista->inicio->idPaginaFantasma == idPagina)
		return fantasmaRemoveLRU(area,list);

	if(lista->inicio != NULL && lista->inicio->idPaginaFantasma == idPagina){
		ElementoFantasma *ret = lista->inicio;
		lista->inicio = ret->cauda;
		int id = ret->idPaginaFantasma;
		free(ret);
		lista->tamanhoAtual--;
		area->tamanhoAtualFantasma--;
		return id;
	}
	ElementoFantasma *elementoAnterior = lista->inicio;
	ElementoFantasma *elementoAtual = lista->inicio->cauda;
	while(elementoAtual != NULL){
		if(elementoAtual->idPaginaFantasma == idPagina){
			elementoAnterior->cauda = elementoAtual->cauda;
			if(elementoAtual->cauda == NULL)
				lista->fim = elementoAnterior;
			lista->tamanhoAtual--;
			area->tamanhoAtualFantasma--;
			int id = elementoAtual->idPaginaFantasma;
			free(elementoAtual);
			return id;
		}
		elementoAnterior = elementoAtual;
		elementoAtual = elementoAtual->cauda;
	}
	return -1;
}




int fantasmaInserir(AreaBuffer *area, int list, int pageId){
	// if(area->tamanhoAtual >= area->tamanhoMaximo || list > 1)
	// 	return 0;

	ListaFantasma *lista = getListFantasma(area,list);
	

	if(lista->tamanhoAtual >= lista->tamanhoMaximo){
		fantasmaRemoveLRU(area,list);
		// printf("Inserido na fatasma: %d\tRemovido Fatasma: %d\n",pageId,fantasmaRemoveLRU(area,list));
	}

	ElementoFantasma *elemento = malloc(sizeof(ElementoFantasma));
	elemento->idPaginaFantasma = pageId;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}


	lista->tamanhoAtual++;
	area->tamanhoAtualFantasma++;
	return 1;
	
}
//Manipulação Lista Fantasma

//Manipulação Lista real
ListaReal* getList(AreaBuffer *area,int list){
	if(list == 0)//RA
		return area->RA;
	else if(list == 1)//FA
		return area->FA;
	else if(list == 2)
		return area->livres;
	else
		return NULL;

}ListaReal* getListContraria(AreaBuffer *area,int list){
	if(list == 1)//RA
		return area->RA;
	else if(list == 0)//FA
		return area->FA;
	else
		return NULL;
}

int realContem(AreaBuffer *area, int list, int idPagina){
	ListaReal *lista = getList(area,list);
	ElementoReal *elemento = lista->inicio;
	while(elemento != NULL){
		if((elemento->bufferCabeca != NULL) && (elemento->bufferCabeca->pagina->idPagina == idPagina))
			return 1;
		elemento = elemento->cauda;
	}
	return 0;
}

Buffer* realRemoveLRU(AreaBuffer *area, int list){
	// printf("%d Removendo LRU\n",list);
	ListaReal *lista = getList(area,list);
	ElementoReal *ret = NULL;
	if(lista->tamanhoAtual > 0){
		ret = lista->inicio;
		if(lista->tamanhoAtual == 1){
			lista->inicio = NULL;
			lista->fim = NULL;
			// printf("Acertou ponteiros\n");
		}else{
			lista->inicio = ret->cauda;
		}

		lista->tamanhoAtual--;
		// printf("Diminui contador\n");
		if(list == 0 || list == 1)
			area->tamanhoAtual--;
		Buffer *bufret = ret->bufferCabeca;
		// printf("BufferID removendo: %d\ttamanho: %d\tLista:%d\n",bufret->idBuffer,lista->tamanhoAtual,list);
		free(ret);
		// printf("Deu free\n");
		return bufret;
	}
	// printf("%d Não fez nada\n",list);
	return NULL;
}

Buffer* realRemovePorIdPagina(AreaBuffer *area, int list, int idPagina){
	ListaReal *lista = getList(area,list);
	if(lista->tamanhoAtual <= 0)
		return NULL;

	if(lista->tamanhoAtual == 1 && lista->inicio->bufferCabeca->pagina->idPagina == idPagina)
		return realRemoveLRU(area,list);

	if(lista->inicio != NULL && lista->inicio->bufferCabeca->pagina->idPagina == idPagina){
		ElementoReal *ret = lista->inicio;
		lista->inicio = ret->cauda;
		lista->tamanhoAtual--;
		area->tamanhoAtual--;
		Buffer *bufret = ret->bufferCabeca;
		free(ret);
		return bufret;
	}
	ElementoReal *elementoAnterior = lista->inicio;
	ElementoReal *elementoAtual = lista->inicio->cauda;
	while(elementoAtual != NULL){
		if(elementoAtual->bufferCabeca->pagina->idPagina == idPagina){
			elementoAnterior->cauda = elementoAtual->cauda;
			if(elementoAtual->cauda == NULL)
				lista->fim = elementoAnterior;
			lista->tamanhoAtual--;
			area->tamanhoAtual--;
			Buffer *bufret = elementoAtual->bufferCabeca;
			free(elementoAtual);
			return bufret;
		}
		elementoAnterior = elementoAtual;
		elementoAtual = elementoAtual->cauda;
	}
	return NULL;
}


void showArea(AreaBuffer *area){
	ListaReal *lista = NULL;
	for(int i = 0 ; i < 3 ;i++){
		lista = getList(area,i);
		printf("lista %d tamanhoAtual %d: ",i,lista->tamanhoAtual);
		ElementoReal *elemento = lista->inicio;
		while(elemento != NULL){
			printf(";buffer %d \t",elemento->bufferCabeca->idBuffer);
			if(elemento->bufferCabeca->pagina != NULL)
				printf("pagina %d \t",elemento->bufferCabeca->pagina->idPagina);
			
			elemento = elemento->cauda;
		}
		printf("\n\n");
	}
}

int realInserirBuffer(AreaBuffer *area,int list ,Buffer *buffer){
	ListaReal *lista = getList(area,list);
	if(area->tamanhoAtual >= area->tamanhoMaximo || list > 2 || lista->tamanhoAtual >= lista->tamanhoMaximo)
		return 0;
	
	ElementoReal *elemento = malloc(sizeof(ElementoReal));
	elemento->bufferCabeca = buffer;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}

	lista->tamanhoAtual++;
	if(list < 2)
		area->tamanhoAtual++;
	return 1;
	
}



Buffer* realInserir(AreaBuffer *area, int list, Pagina *pagina){
	// if(area->tamanhoAtual >= area->tamanhoMaximo || list > 1)
	// 	return 0;

	ListaReal *lista = getList(area,list);
	
	if(area->tamanhoAtual < area->tamanhoMaximo && lista->tamanhoAtual < lista->tamanhoMaximo){//Existe buffer livre na área
		Buffer *buffer = realRemoveLRU(area,2);
		buffer->pagina = pagina;
		realInserirBuffer(area,list,buffer);
		return buffer;
	}else if(list >= 2)
		return NULL;

	//Passa LRU para fantasma
	// printf("vai remover LRU para mandar para zona fantasma %d\n",lista->tamanhoMaximo);
	// showArea(area);
	Buffer *buffer = realRemoveLRU(area,list);
	// printf("BufferID: %d\n",buffer->idBuffer);
	int idPagina = buffer->pagina->idPagina;
	fantasmaInserir(area,list,idPagina);
	// printf("Está cheio \n",);
	// free(buffer->pagina);
	buffer->pagina = pagina;

	//Insere pagina na lista
	ElementoReal *elemento = malloc(sizeof(ElementoReal));
	elemento->bufferCabeca = buffer;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}


	lista->tamanhoAtual++;
	area->tamanhoAtual++;
	return buffer;
	
}


//Manipulação Lista Real



int hit,op;


void normalizarLista(AreaBuffer *area,int list){
	ListaReal *lista = getList(area,list);
	ListaReal *oposta = getList(area,!list);
	
	// ListaFantasma *fantasma = getListFantasma(area,list);
	// ListaFantasma *fantasmaOposta = getListFantasma(area,!list);

	while(lista->tamanhoMaximo <= 0){
		lista->tamanhoMaximo++;
		oposta->tamanhoMaximo--;

		// fantasma->tamanhoMaximo++;
		// fantasmaOposta->tamanhoMaximo--;
	}

	while(oposta->tamanhoMaximo <= 0){
		lista->tamanhoMaximo--;
		oposta->tamanhoMaximo++;

		// fantasma->tamanhoMaximo--;
		// fantasmaOposta->tamanhoMaximo++;
	}

	while(lista->tamanhoAtual > lista->tamanhoMaximo){
		Buffer *buffer = realRemoveLRU(area,list);
		buffer->pagina = NULL;
		realInserirBuffer(area,2,buffer);
	}
	// while(fantasma->tamanhoAtual > fantasma->tamanhoMaximo){
	// 	fantasmaRemoveLRU(area,list);
	// }
}

void conselhoListas(BufferPool *pool,int tipo){//tipo = 0 leitura, 1 escrita
	int conselho;
	Buffer *buffer = NULL;

	if(tipo == 0){
		if(pool->conselheiroFrequenciaLeitura > 0){//Aumenta área de frequencia
			// printf("aumenta frequencia leitura\n");

			pool->conselheiroFrequenciaLeitura--;

			pool->areaLeitura->FA->tamanhoMaximo++;
			// pool->areaLeitura->GFA->tamanhoMaximo++;
			
			while(pool->areaLeitura->FA->tamanhoMaximo + pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo){
				pool->areaLeitura->RA->tamanhoMaximo--;
				// pool->areaLeitura->GRA->tamanhoMaximo--;
			}
		}else{//Aumenta área de recentes
			// printf("aumenta recencia leitura\n");

			pool->conselheiroFrequenciaLeitura++;

			pool->areaLeitura->RA->tamanhoMaximo++;
			// pool->areaLeitura->GRA->tamanhoMaximo++;

			while(pool->areaLeitura->FA->tamanhoMaximo + pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo){
				pool->areaLeitura->FA->tamanhoMaximo--;
				// pool->areaLeitura->GFA->tamanhoMaximo--;
			}
		}
	}else if(tipo == 1){

		if(pool->conselheiroFrequenciaEscrita > 0){//Aumenta área de frequencia
			// printf("aumenta frequencia escrita\n");
			
			pool->conselheiroFrequenciaEscrita--;

			pool->areaEscrita->FA->tamanhoMaximo++;
			// pool->areaEscrita->GFA->tamanhoMaximo++;

			while(pool->areaEscrita->FA->tamanhoMaximo + pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo){
				pool->areaEscrita->RA->tamanhoMaximo--;
				// pool->areaEscrita->GRA->tamanhoMaximo--;
			}
		}else{//Aumenta área de recentes
			// printf("aumenta recencia escrita\n");

			pool->conselheiroFrequenciaEscrita++;

			pool->areaEscrita->RA->tamanhoMaximo++;
			// pool->areaEscrita->GRA->tamanhoMaximo++;

			while(pool->areaEscrita->FA->tamanhoMaximo + pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo){
				pool->areaEscrita->FA->tamanhoMaximo--;
				// pool->areaEscrita->GFA->tamanhoMaximo--;
			}
		}
	}
}


void seguirConselhos(BufferPool *pool){
	
	int tamanhoAteriorLeitura = pool->areaLeitura->tamanhoMaximo;
	int tamanhoAteriorEscrita = pool->areaEscrita->tamanhoMaximo;

	while(pool->conselheiroAreaEscrita != 0){
		if(pool->conselheiroAreaEscrita > 0){//Área de escrita deve aumentar
			// printf("Aumentando área escrita\n");
			pool->conselheiroAreaEscrita--;

			pool->areaEscrita->tamanhoMaximo++;
			pool->areaEscrita->livres->tamanhoMaximo++;

			pool->areaLeitura->livres->tamanhoMaximo--;
			pool->areaLeitura->tamanhoMaximo--;

			Buffer *buffer = realRemoveLRU(pool->areaLeitura,2);
			if(buffer == NULL){//remove um buffer livre da leitura
				//não há buffer de leitura livre
				if(pool->conselheiroFrequenciaLeitura < 0){
					// printf("remove de frequencia no conselho\n");
					buffer = realRemoveLRU(pool->areaLeitura,1);
				}else{//dá preferência para área de frquência
					buffer = realRemoveLRU(pool->areaLeitura,0);
				}
			}
			
			buffer->pagina = NULL;
			realInserirBuffer(pool->areaEscrita,2,buffer);

		}else{//Área de leitura deve aumentar
			pool->conselheiroAreaEscrita++;

			pool->areaLeitura->tamanhoMaximo++;
			pool->areaLeitura->livres->tamanhoMaximo++;

			pool->areaEscrita->tamanhoMaximo--;
			pool->areaEscrita->livres->tamanhoMaximo--;

			// printf("Aumentando área Leitura\n");

			Buffer *buffer = realRemoveLRU(pool->areaEscrita,2);
			if(buffer == NULL){//remove um buffer livre da escrita
				//não há buffer de escrita livre
				if(pool->conselheiroFrequenciaEscrita < 0){
					// printf("remove de frequencia no conselho\n");
					buffer = realRemoveLRU(pool->areaEscrita,1);
				}else{//dá preferência para área de frquência
					buffer = realRemoveLRU(pool->areaEscrita,0);
				}
			}
			buffer->pagina = NULL;
			realInserirBuffer(pool->areaLeitura,2,buffer);
		}
	}

	int deltaLeitura = pool->areaLeitura->tamanhoMaximo - tamanhoAteriorLeitura;
	int deltaEscrita = pool->areaEscrita->tamanhoMaximo - tamanhoAteriorEscrita;

	int deltaLeituraRA;
	int deltaLeituraFA;

	if(deltaLeitura % 2 == 0){
		deltaLeituraRA = deltaLeitura;
		deltaLeituraFA = deltaLeitura;
	}else{
		int deltaMaior;
		int deltaMenor;
		if(deltaLeitura > 0){
			deltaMaior = deltaLeitura;
			deltaMenor = deltaLeitura - 1;
		}else{
			deltaMaior = deltaLeitura + 1;
			deltaMenor = deltaLeitura;
		}
		if(pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->FA->tamanhoMaximo){
			deltaLeituraRA = deltaMaior;
			deltaLeituraFA = deltaMenor;
		}else{
			deltaLeituraRA = deltaMenor;
			deltaLeituraFA = deltaMaior;
		}
	}

	int deltaEscritaRA;
	int deltaEscritaFA;

	if(deltaEscrita % 2 == 0){
		deltaEscritaRA = deltaEscrita;
		deltaEscritaFA = deltaEscrita;
	}else{
		int deltaMaior;
		int deltaMenor;
		if(deltaEscrita > 0){
			deltaMaior = deltaEscrita;
			deltaMenor = deltaEscrita - 1;
		}else{
			deltaMaior = deltaEscrita + 1;
			deltaMenor = deltaEscrita;
		}
		if(pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->FA->tamanhoMaximo){
			deltaEscritaRA = deltaMaior;
			deltaEscritaFA = deltaMenor;
		}else{
			deltaEscritaRA = deltaMenor;
			deltaEscritaFA = deltaMaior;
		}
	}


	pool->areaLeitura->RA->tamanhoMaximo += deltaLeituraRA;
	pool->areaLeitura->FA->tamanhoMaximo += deltaLeituraFA;
	// pool->areaLeitura->GRA->tamanhoMaximo = pool->areaLeitura->RA->tamanhoMaximo;
	// pool->areaLeitura->GFA->tamanhoMaximo = pool->areaLeitura->FA->tamanhoMaximo;

	pool->areaEscrita->RA->tamanhoMaximo += deltaEscritaRA;
	pool->areaEscrita->FA->tamanhoMaximo += deltaEscritaFA;
	// pool->areaEscrita->GRA->tamanhoMaximo = pool->areaEscrita->RA->tamanhoMaximo;
	// pool->areaEscrita->GFA->tamanhoMaximo = pool->areaEscrita->FA->tamanhoMaximo;

	while(pool->conselheiroFrequenciaEscrita != 0 ){
		conselhoListas(pool,1);
	}

	while(pool->conselheiroFrequenciaLeitura != 0 ){
		// printf("vai seguir conselho %d\n",pool->conselheiroFrequenciaLeitura);
		conselhoListas(pool,0);
	}

	normalizarLista(pool->areaLeitura,0);
	normalizarLista(pool->areaLeitura,1);
	normalizarLista(pool->areaEscrita,0);
	normalizarLista(pool->areaEscrita,1);

}


int atualizarConselheiros(BufferPool *pool, int idBlock){
	int id = -1;
	id = fantasmaRemovePorIdPagina(pool->areaLeitura,0,idBlock);
	if(id != -1){//Estava na lista fantasma de leitura recente.
		// printf("conselho lr\n");
		// ListaReal *oposta = getList(pool->areaLeitura,1);
		if(pool->areaEscrita->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo / 4){
		// if(pool->areaEscrita->tamanhoAtual < (int)  pool->areaEscrita->tamanhoMaximo/2 ){//aconselha aumentar lista de leitura
			pool->conselheiroAreaEscrita--;
		}
		pool->conselheiroFrequenciaLeitura--;

	}else{
		id = fantasmaRemovePorIdPagina(pool->areaLeitura,1,idBlock);
		if(id != -1){//Estava na lista fantasma de leitura frequente.
			// printf("conselho lf\n");
			// ListaReal *oposta = getList(pool->areaLeitura,0);
			if(pool->areaEscrita->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo / 4){
			// if(pool->areaEscrita->tamanhoAtual < (int)  pool->areaEscrita->tamanhoMaximo/2){//aconselha aumentar lista de leitura
				pool->conselheiroAreaEscrita--;
			}
			pool->conselheiroFrequenciaLeitura++;
		}else{
			id = fantasmaRemovePorIdPagina(pool->areaEscrita,0,idBlock);
			if(id != -1){//Estava na lista fantasma de escrita recente.
				// printf("conselho er\n");
				// ListaReal *oposta = getList(pool->areaEscrita,1);
				if(pool->areaLeitura->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo / 4){
				// if(pool->areaLeitura->tamanhoAtual < (int)  pool->areaLeitura->tamanhoMaximo/2 ){//aconselha aumentar lista de escrita
					pool->conselheiroAreaEscrita++;
				}
				pool->conselheiroFrequenciaEscrita--;
			}else{
				// if(opt != 1)
				// 	return -1;
				id = fantasmaRemovePorIdPagina(pool->areaEscrita,1,idBlock);
				if(id != -1){//Estava na lista fantasma de escrita frequente
					// printf("conselho ef\n");
					// ListaReal *oposta = getList(pool->areaEscrita,0);
					if(pool->areaLeitura->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo / 4){
					// if(pool->areaLeitura->tamanhoAtual < (int)  pool->areaLeitura->tamanhoMaximo/2 ){//aconselha aumentar lista de leitura
						pool->conselheiroAreaEscrita++;
					}
					pool->conselheiroFrequenciaEscrita++;
				}
			}
		}
	}
	return id==idBlock;
}


Buffer* marcarBufferEscrita(BufferPool *pool,int opWrite){
	Buffer *buffer;
	buffer = realRemovePorIdPagina(pool->areaEscrita,1,opWrite);
	if(buffer != NULL){//Verifuca se buffer já havia sido adicionado em escrita frequente
		realInserirBuffer(pool->areaEscrita,1,buffer);
		return buffer;
	}

	// printf("escrevendo\n");
	buffer = realRemovePorIdPagina(pool->areaLeitura,0,opWrite);
	if(buffer != NULL){
		// printf("tirando de recencia\n");
		Pagina *pagina = buffer->pagina;
		buffer->pagina = NULL;
		realInserirBuffer(pool->areaLeitura,2,buffer);
		// printf("inseriu nos livres\n");
		if(pagina->usos > 1)
			buffer = realInserir(pool->areaEscrita,1,pagina);
		else
			buffer = realInserir(pool->areaEscrita,0,pagina);
		// printf("Esta escrevendo %d\n",opWrite);
	}else{
		buffer = realRemovePorIdPagina(pool->areaLeitura,1,opWrite);
		if(buffer != NULL){
			Pagina *pagina = buffer->pagina;
			realInserirBuffer(pool->areaLeitura,2,buffer);
			buffer = realInserir(pool->areaEscrita,1,pagina);
		}	
	}
	return buffer;
}

Buffer* allocBuffer(BufferPool *pool,int idBlock){
	int estavaFantasma = atualizarConselheiros(pool,idBlock);
	Buffer *buffer = NULL;
	if(estavaFantasma != 0 ){//estava em lista fantasma
		buffer = realInserir(pool->areaLeitura,1,NULL);

	}
	else{
		// printf("leitura!!!!\n");
		buffer = realInserir(pool->areaLeitura,0,NULL);
		
	}
		
	return buffer;
}

Buffer* getBlock(BufferPool *pool,int idBlock,Pagina* paginas){
	op++;//Conta operação
	Buffer *block = NULL;
	if(!(pool->areaLeitura->tamanhoAtual + pool->areaLeitura->tamanhoMaximoFantasma >= BUFFER_SIZE)){//Basta olhar apenas na de leitura, pois a página só é escrita posteriormente
		seguirConselhos(pool);
	}
	//Listas de frequência
	block = realRemovePorIdPagina(pool->areaLeitura,1,idBlock);
	if(block != NULL){
		realInserirBuffer(pool->areaLeitura,1,block);
		// printf("frequencia leitura\n");
	}
	else{
		block = realRemovePorIdPagina(pool->areaEscrita,1,idBlock);
		if(block != NULL){
			realInserirBuffer(pool->areaEscrita,1,block);
			// printf("frequencia escrita\n");		
		}
	}
	//Listas de frequência

	//Listas de resência
	if(block == NULL){
		block = realRemovePorIdPagina(pool->areaLeitura,0,idBlock);
		if(block != NULL){
			realInserirBuffer(pool->areaLeitura,2,block);
			Pagina *pg = block->pagina;
			block->pagina = NULL;
			block = realInserir(pool->areaLeitura,1,pg);
			// printf("resencia leitura\n");
		}else{
			block = realRemovePorIdPagina(pool->areaEscrita,0,idBlock);
			if(block != NULL){
				realInserirBuffer(pool->areaEscrita,2,block);
				Pagina *pg = block->pagina;
				block->pagina = NULL;
				block = realInserir(pool->areaEscrita,1,pg);
				// printf("resencia escrita buffer %d\tpagina %d\n",block->idBuffer,block->pagina->idPagina);
			}
		}	
	}
	//Listas de resência

	//Não estava no buffer
	if(block == NULL){
		block = allocBuffer(pool,idBlock);
		//Busca do disco
		if(block == NULL)
			printf("não achou buffer para %d",idBlock);

		block->pagina = &paginas[idBlock];
		// printf("colocou pagina %d no buffer %d\n",block->pagina->idPagina,block->idBuffer);
	}else
		hit++;//Conta como hit de memória
	

	return block;
}



int main(int nag, char *argv[]){
	hit = 0;
	op = 0;
	bufferPool = malloc(sizeof(BufferPool));
	initBufferPool(bufferPool);
	// teste();
	FILE *file;
	if(nag <= 1)
		file = fopen("workload.txt","r");
	else
		file = fopen(argv[1],"r");
	int sizePagina;
	fscanf(file,"%d\n",&sizePagina);
	
	Pagina paginas[sizePagina];
	for(int i = 0 ; i < sizePagina ; i++)
		paginas[i] = (Pagina){.idPagina = i , .escritas = 0 , .usos = 0};
	

	int pagina;
	char opt;
	Buffer *buf = NULL;


	while(fscanf(file,"%d %c",&pagina,&opt) != EOF){

		// printf("vai começar a ler  %d %c\n",pagina,opt);
		buf = getBlock(bufferPool,pagina,paginas);
		if(opt == 'w'){
			// printf("opt %c\n",opt);
			buf = marcarBufferEscrita(bufferPool, pagina);
		}
			// showArea(bufferPool->areaLeitura);
			// showArea(bufferPool->areaEscrita);
		if( buf == NULL){
			printf("não consegui ler o buffer na operação %d %c\n",pagina,opt);
			return 1;
		}
		else
			if(buf->pagina == NULL){

				printf("não consegui ler a pagina na operação %d %c\n",pagina,opt);
				return 1;
			}
			else{
				buf->pagina->usos++;
			}

		// printf("area de leitura MAX:%d\tCUR:%d\n",bufferPool->areaLeitura->tamanhoMaximo,bufferPool->areaLeitura->tamanhoAtual);
		// printf("lista recentes MAX:%d\tCUR:%d\n",bufferPool->areaLeitura->RA->tamanhoMaximo,bufferPool->areaLeitura->RA->tamanhoAtual);
		// printf("lista frequentes MAX:%d\tCUR:%d\n",bufferPool->areaLeitura->FA->tamanhoMaximo,bufferPool->areaLeitura->FA->tamanhoAtual);
		// printf("lista livres MAX:%d\tCUR:%d\n",bufferPool->areaLeitura->livres->tamanhoMaximo,bufferPool->areaLeitura->livres->tamanhoAtual);
		// printf("lista fantasma recentes MAX:%d\tCUR:%d\n",bufferPool->areaLeitura->GRA->tamanhoMaximo,bufferPool->areaLeitura->GRA->tamanhoAtual);
		// printf("lista fantasma frequentes MAX:%d\tCUR:%d\n\n",bufferPool->areaLeitura->GFA->tamanhoMaximo,bufferPool->areaLeitura->GFA->tamanhoAtual);

		// printf("area de escrita MAX:%d\tCUR:%d\n",bufferPool->areaEscrita->tamanhoMaximo,bufferPool->areaEscrita->tamanhoAtual);
		// printf("lista recentes MAX:%d\tCUR:%d\n",bufferPool->areaEscrita->RA->tamanhoMaximo,bufferPool->areaEscrita->RA->tamanhoAtual);
		// printf("lista frequentes MAX:%d\tCUR:%d\n",bufferPool->areaEscrita->FA->tamanhoMaximo,bufferPool->areaEscrita->FA->tamanhoAtual);
		// printf("lista livres MAX:%d\tCUR:%d\n",bufferPool->areaEscrita->livres->tamanhoMaximo,bufferPool->areaEscrita->livres->tamanhoAtual);
		// printf("lista fantasma recentes MAX:%d\tCUR:%d\n",bufferPool->areaEscrita->GRA->tamanhoMaximo,bufferPool->areaEscrita->GRA->tamanhoAtual);
		// printf("lista fantasma frequentes MAX:%d\tCUR:%d\n\n\n\n",bufferPool->areaEscrita->GFA->tamanhoMaximo,bufferPool->areaEscrita->GFA->tamanhoAtual);


	}
	fclose(file);


	printf("hit: %d\nop=%d\n%f\n",hit,op,(double)(hit*100)/(double)op);
	return 0;
}